/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "Common/Compat.h"
#include <cassert>
#include <vector>

extern "C" {
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
}

#include <boost/algorithm/string/predicate.hpp>

#include "Common/Config.h"
#include "Common/Error.h"
#include "Common/FileUtils.h"
#include "Common/Logger.h"
#include "Common/StringExt.h"
#include "Common/md5.h"

#include "Hypertable/Lib/CompressorFactory.h"

#include "BlockCompressionCodec.h"
#include "CommitLog.h"
#include "CommitLogBlockStream.h"
#include "CommitLogReader.h"

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

namespace {
  struct ByFragmentNumber {
    bool operator()(const String &x, const String &y) const {
      int num_x = atoi(x.c_str());
      int num_y = atoi(y.c_str());
      return num_x < num_y;
    }
  };
}


CommitLogReader::CommitLogReader(FilesystemPtr &fs, const String &log_dir)
  : CommitLogBase(log_dir), m_fs(fs), m_fragment_queue_offset(0),
    m_block_buffer(256), m_revision(TIMESTAMP_MIN), m_compressor(0) {

  if (get_bool("Hypertable.CommitLog.SkipErrors"))
    CommitLogBlockStream::ms_assert_on_error = false;

  load_fragments(m_log_dir, 0);
  reset();
}


CommitLogReader::~CommitLogReader() {
}


bool
CommitLogReader::next_raw_block(CommitLogBlockInfo *infop,
                                BlockCompressionHeaderCommitLog *header) {
  LogFragmentQueue::iterator fragment_queue_iter;

  try_again:
  fragment_queue_iter = m_fragment_queue.begin() + m_fragment_queue_offset;
  if (fragment_queue_iter == m_fragment_queue.end())
    return false;

  if ((*fragment_queue_iter)->block_stream == 0)
    (*fragment_queue_iter)->block_stream =
      new CommitLogBlockStream(m_fs, (*fragment_queue_iter)->log_dir,
                               format("%u", (*fragment_queue_iter)->num));

  if (!(*fragment_queue_iter)->block_stream->next(infop, header)) {
    CommitLogFileInfo *info = *fragment_queue_iter;
    delete info->block_stream;
    info->block_stream = 0;
    if (m_revision == TIMESTAMP_MIN) {
      HT_WARNF("Skipping log fragment '%s/%u' because unable to read any valid blocks",
               info->log_dir.c_str(), info->num);
      m_fragment_queue.erase(fragment_queue_iter);
    }
    else {
      info->revision = m_revision;
      m_fragment_queue_offset++;
    }
    m_revision = TIMESTAMP_MIN;
    goto try_again;
  }

  if (header->check_magic(CommitLog::MAGIC_LINK)) {
    assert(header->get_compression_type() == BlockCompressionCodec::NONE);
    String log_dir = (const char *)(infop->block_ptr + header->length());
    boost::trim_right_if(log_dir, boost::is_any_of("/"));
    load_fragments(log_dir, *fragment_queue_iter);
    m_linked_logs.insert(md5_hash(log_dir.c_str()));
    if (header->get_revision() > m_latest_revision)
      m_latest_revision = header->get_revision();
    if (header->get_revision() > m_revision)
      m_revision = header->get_revision();
    goto try_again;
  }

  HT_INFOF("Replaying commit log fragment %s/%u", (*fragment_queue_iter)->log_dir.c_str(),
	   (*fragment_queue_iter)->num);

  return true;
}


bool
CommitLogReader::next(const uint8_t **blockp, size_t *lenp,
                      BlockCompressionHeaderCommitLog *header) {
  CommitLogBlockInfo binfo;

  while (next_raw_block(&binfo, header)) {

    if (binfo.error == Error::OK) {
      DynamicBuffer zblock(0, false);

      m_block_buffer.clear();
      zblock.base = binfo.block_ptr;
      zblock.ptr = binfo.block_ptr + binfo.block_len;

      try {
        load_compressor(header->get_compression_type());
        m_compressor->inflate(zblock, m_block_buffer, *header);
      }
      catch (Exception &e) {
        LogFragmentQueue::iterator iter = m_fragment_queue.begin() + m_fragment_queue_offset;
        HT_ERRORF("Inflate error in CommitLog fragment %s starting at "
                  "postion %lld (block len = %lld) - %s",
                  (*iter)->block_stream->get_fname().c_str(),
                  (Lld)binfo.start_offset, (Lld)(binfo.end_offset
                  - binfo.start_offset), Error::get_text(e.code()));
        continue;
      }

      if (header->get_revision() > m_latest_revision)
        m_latest_revision = header->get_revision();

      if (header->get_revision() > m_revision)
        m_revision = header->get_revision();

      *blockp = m_block_buffer.base;
      *lenp = m_block_buffer.fill();
      return true;
    }

    LogFragmentQueue::iterator iter = m_fragment_queue.begin() + m_fragment_queue_offset;
    HT_WARNF("Corruption detected in CommitLog fragment %s starting at "
             "postion %lld for %lld bytes - %s",
             (*iter)->block_stream->get_fname().c_str(),
             (Lld)binfo.start_offset, (Lld)(binfo.end_offset
             - binfo.start_offset), Error::get_text(binfo.error));
  }

  struct LtClfip swo;
  sort(m_fragment_queue.begin(), m_fragment_queue.end(), swo);

  return false;
}


void CommitLogReader::load_fragments(String log_dir, CommitLogFileInfo *parent) {
  vector<string> listing;
  CommitLogFileInfo *fi;
  bool added_fragments = false;
  int mark = -1;

  try {
    m_fs->readdir(log_dir, listing);
  }
  catch (Hypertable::Exception &e) {
    if (e.code() == Error::DFSBROKER_BAD_FILENAME) {
      HT_INFOF("Skipping directory '%s' because it does not exist",
               log_dir.c_str());
      return;
    }
    HT_THROW2(e.code(), e, e.what());
  }

  if (listing.size() == 0)
    return;

  sort(listing.begin(), listing.end(), ByFragmentNumber());

  for (size_t i=0; i<listing.size(); i++) {

    if (boost::ends_with(listing[i], ".tmp"))
      continue;

    if (boost::ends_with(listing[i], ".mark")) {
      mark = atoi(listing[i].c_str());
      continue;
    }

    char *endptr;
    long num = strtol(listing[i].c_str(), &endptr, 10);
    if (*endptr != 0) {
      HT_WARNF("Invalid file '%s' found in commit log directory '%s'",
               listing[i].c_str(), log_dir.c_str());
    }
    else {
      fi = new CommitLogFileInfo();
      fi->num = (uint32_t)num;
      fi->log_dir = log_dir;
      fi->log_dir_hash = md5_hash(log_dir.c_str());
      fi->size = m_fs->length(log_dir + "/" + listing[i]);
      fi->parent = parent;
      if (parent)
        parent->references++;
      if (fi->size > 0) {
        m_fragment_queue.push_back(fi);
        added_fragments = true;
      }
    }
  }

  if (mark != -1) {
    if (m_fragment_queue.empty() || mark < (int)m_fragment_queue.front()->num) {
      String mark_filename;
      try {
	mark_filename = log_dir + "/" + mark + ".mark";
	m_fs->remove(mark_filename);
      }
      catch (Hypertable::Exception &e) {
	HT_FATALF("Problem removing mark file '%s' - %s", mark_filename.c_str(), e.what());
      }
    }
    else
      m_range_reference_required = false;
  }

  // Add this log dir to the parent's purge_dirs set
  if (parent)
    parent->purge_dirs.insert(log_dir);

}


void CommitLogReader::load_compressor(uint16_t ztype) {
  BlockCompressionCodecPtr compressor_ptr;

  if (m_compressor != 0 && ztype == m_compressor_type)
    return;

  if (ztype >= BlockCompressionCodec::COMPRESSION_TYPE_LIMIT)
    HT_THROWF(Error::BLOCK_COMPRESSOR_UNSUPPORTED_TYPE,
              "Invalid compression type '%d'", (int)ztype);

  compressor_ptr = m_compressor_map[ztype];

  if (!compressor_ptr) {
    compressor_ptr = CompressorFactory::create_block_codec(
        (BlockCompressionCodec::Type)ztype);
    m_compressor_map[ztype] = compressor_ptr;
  }

  m_compressor_type = ztype;
  m_compressor = compressor_ptr.get();
}
