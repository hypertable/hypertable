/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

/// @file
/// Definitions for CommitLogReader.
/// This file contains definitions for CommitLogReader, a class for
/// sequentially reading a commit log.

#include <Common/Compat.h>

#include "CommitLogReader.h"

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>
#include <Common/StringExt.h>
#include <Common/md5.h>

#include <Hypertable/Lib/BlockCompressionCodec.h>
#include <Hypertable/Lib/CommitLog.h>
#include <Hypertable/Lib/CommitLogBlockStream.h>
#include <Hypertable/Lib/CompressorFactory.h>

#include <boost/algorithm/string/predicate.hpp>

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

using namespace Hypertable;
using namespace std;

namespace {
  struct ByFragmentNumber {
    bool operator()(const Filesystem::Dirent &x, const Filesystem::Dirent &y) const {
      int num_x = atoi(x.name.c_str());
      int num_y = atoi(y.name.c_str());
      return num_x < num_y;
    }
  };
}

CommitLogReader::CommitLogReader(FilesystemPtr &fs, const string &log_dir)
  : CommitLogBase(log_dir), m_fs(fs), m_block_buffer(256),
    m_revision(TIMESTAMP_MIN), m_last_fragment_id(-1) {
  if (Config::properties->get_bool("Hypertable.CommitLog.SkipErrors"))
    CommitLogBlockStream::ms_assert_on_error = false;
  load_fragments(m_log_dir, 0);
  reset();
}

CommitLogReader::CommitLogReader(FilesystemPtr &fs, const string &log_dir,
                                 const std::vector<int32_t> &fragment_filter)
  : CommitLogBase(log_dir), m_fs(fs), m_block_buffer(256),
    m_revision(TIMESTAMP_MIN),
    m_fragment_filter(fragment_filter.begin(), fragment_filter.end()),
    m_last_fragment_id(-1) {
  if (Config::properties->get_bool("Hypertable.CommitLog.SkipErrors"))
    CommitLogBlockStream::ms_assert_on_error = false;
  load_fragments(log_dir, 0);
  reset();
}

bool
CommitLogReader::next_raw_block(CommitLogBlockInfo *infop,
                                BlockHeaderCommitLog *header) {
  LogFragmentQueue::iterator fragment_queue_iter;

  try_again:
  fragment_queue_iter = m_fragment_queue.begin() + m_fragment_queue_offset;
  if (fragment_queue_iter == m_fragment_queue.end())
    return false;

  if ((*fragment_queue_iter)->block_stream == 0) {
    (*fragment_queue_iter)->block_stream =
      new CommitLogBlockStream(m_fs, (*fragment_queue_iter)->log_dir,
                               format("%u", (*fragment_queue_iter)->num));
    m_last_fragment_fname = (*fragment_queue_iter)->block_stream->get_fname();
    m_last_fragment_id = (int32_t)toplevel_fragment_id(*fragment_queue_iter);
  }

  if (!(*fragment_queue_iter)->block_stream->next(infop, header)) {
    CommitLogFileInfo *info = *fragment_queue_iter;
    delete info->block_stream;
    info->block_stream = 0;

    if (m_revision == TIMESTAMP_MIN) {
      if (m_verbose)
        HT_INFOF("Skipping log fragment '%s/%u' because unable to read any "
                 " valid blocks", info->log_dir.c_str(), info->num);
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
    string log_dir = (const char *)(infop->block_ptr + header->encoded_length());
    boost::trim_right_if(log_dir, boost::is_any_of("/"));
    m_linked_log_hashes.insert(md5_hash(log_dir.c_str()));
    m_linked_logs.insert(log_dir);
    load_fragments(log_dir, *fragment_queue_iter);
    if (header->get_revision() > m_latest_revision)
      m_latest_revision = header->get_revision();
    if (header->get_revision() > m_revision)
      m_revision = header->get_revision();
    goto try_again;
  }

  if (m_verbose)
    HT_INFOF("Replaying commit log fragment %s/%u", (*fragment_queue_iter)->log_dir.c_str(),
             (*fragment_queue_iter)->num);

  return true;
}

void CommitLogReader::get_init_fragment_ids(vector<uint32_t> &ids) {
  for (auto id : m_init_fragments) {
    ids.push_back((uint32_t)id);
  }
}

bool
CommitLogReader::next(const uint8_t **blockp, size_t *lenp,
                      BlockHeaderCommitLog *header) {
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
  vector<Filesystem::Dirent> listing;
  CommitLogFileInfo *fi;
  int mark = -1;

#if 0
  HT_DEBUG_OUT << "Reading fragments in " << log_dir 
      << " which is part of CommitLog " << m_log_dir 
      << " mark_for_deletion=" << mark_for_deletion 
      << " m_fragment_filter.size()=" << m_fragment_filter.size() << HT_END;
#endif
  try {
    m_fs->readdir(log_dir, listing);
  }
  catch (Hypertable::Exception &e) {
    if (e.code() == Error::FSBROKER_BAD_FILENAME) {
      if (m_verbose)
        HT_INFOF("Skipping directory '%s' because it does not exist",
                 log_dir.c_str());
      return;
    }
    HT_THROW2(e.code(), e, e.what());
  }

  if (listing.size() == 0)
    return;

  sort(listing.begin(), listing.end(), ByFragmentNumber());

  for (size_t i = 0; i < listing.size(); i++) {
    if (boost::ends_with(listing[i].name, ".tmp"))
      continue;

    if (boost::ends_with(listing[i].name, ".mark")) {
      mark = atoi(listing[i].name.c_str());
      continue;
    }

    char *endptr;
    int32_t num = (int32_t)strtol(listing[i].name.c_str(), &endptr, 10);
    if (m_fragment_filter.size() && log_dir == m_log_dir &&
      m_fragment_filter.find(num) == m_fragment_filter.end()) {
      if (m_verbose)
        HT_INFOF("Dropping log fragment %s/%d because it is filtered",
                 log_dir.c_str(), num);
      //HT_DEBUG_OUT << "Fragments " << num <<" in " << log_dir
      //    << " is part of CommitLog "
      //    << m_log_dir << " is not in fragment filter of size()="
      //    << m_fragment_filter.size() << " num_listings=" << listing.size()
      //    << " added_fragments=" << added_fragments << HT_END;
      continue;
    }

    if (*endptr != 0) {
      HT_WARNF("Invalid file '%s' found in commit log directory '%s'",
               listing[i].name.c_str(), log_dir.c_str());
    }
    else {
      fi = new CommitLogFileInfo();
      fi->num = (uint32_t)num;
      fi->log_dir = log_dir;
      fi->log_dir_hash = md5_hash(log_dir.c_str());
      fi->size = m_fs->length(log_dir + "/" + listing[i].name);
      fi->parent = parent;
      if (fi->size > CommitLogBlockStream::header_size()) {
        if (parent)
          parent->references++;
        m_fragment_queue.push_back(fi);
      }
      else {
        delete fi;
        fi = 0;
      }
    }
  }

  if (mark != -1) {
    if (m_fragment_queue.empty() || mark < (int)m_fragment_queue.front()->num) {
      string mark_filename;
      try {
        mark_filename = log_dir + "/" + mark + ".mark";
        m_fs->remove(mark_filename);
      }
      catch (Hypertable::Exception &e) {
        HT_FATALF("Problem removing mark file '%s' - %s",
                mark_filename.c_str(), e.what());
      }
    }
    else
      m_range_reference_required = false;
  }

  // Add this log dir to the parent's purge_dirs set or
  // initialize m_init_fragments vector if no parent
  if (parent)
    parent->purge_dirs.insert(log_dir);
  else {
    m_init_fragments.clear();
    for (const auto fragment : m_fragment_queue)
      m_init_fragments.push_back(fragment->num);
  }

}

void CommitLogReader::load_compressor(uint16_t ztype) {

  if (m_compressor && ztype == m_compressor_type)
    return;

  if (ztype >= BlockCompressionCodec::COMPRESSION_TYPE_LIMIT)
    HT_THROWF(Error::BLOCK_COMPRESSOR_UNSUPPORTED_TYPE,
              "Invalid compression type '%d'", (int)ztype);

  m_compressor = m_compressor_map[ztype];

  if (!m_compressor) {
    m_compressor.reset(CompressorFactory::create_block_codec((BlockCompressionCodec::Type)ztype));
    m_compressor_map[ztype] = m_compressor;
  }

  m_compressor_type = ztype;
}

