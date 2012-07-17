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

#include "Common/Checksum.h"
#include "Common/Config.h"
#include "Common/DynamicBuffer.h"
#include "Common/Error.h"
#include "Common/FileUtils.h"
#include "Common/Logger.h"
#include "Common/StringExt.h"
#include "Common/md5.h"

#include "AsyncComm/Protocol.h"

#include "Hypertable/Lib/CompressorFactory.h"
#include "Hypertable/Lib/BlockCompressionCodec.h"
#include "Hypertable/Lib/BlockCompressionHeaderCommitLog.h"

#include "CommitLog.h"
#include "CommitLogReader.h"

using namespace Hypertable;

const char CommitLog::MAGIC_DATA[10] =
    { 'C','O','M','M','I','T','D','A','T','A' };
const char CommitLog::MAGIC_LINK[10] =
    { 'C','O','M','M','I','T','L','I','N','K' };


CommitLog::CommitLog(FilesystemPtr &fs, const String &log_dir, bool is_meta)
  : CommitLogBase(log_dir), m_fs(fs) {
  initialize(log_dir, Config::properties, 0, is_meta);
}

CommitLog::~CommitLog() {
  delete m_compressor;
  close();
}

void
CommitLog::initialize(const String &log_dir, PropertiesPtr &props,
                      CommitLogBase *init_log, bool is_meta) {
  String compressor;

  m_log_dir = log_dir;
  m_cur_fragment_length = 0;
  m_cur_fragment_num = 0;
  m_needs_roll = false;
  m_replication = -1;

  if (is_meta)
    m_replication = props->get_i32("Hypertable.Metadata.Replication");
  else
    m_replication = props->get_i32("Hypertable.RangeServer.Data.DefaultReplication");

  SubProperties cfg(props, "Hypertable.CommitLog.");

  HT_TRY("getting commit log properites",
    m_max_fragment_size = cfg.get_i64("RollLimit");
    compressor = cfg.get_str("Compressor"));

  m_compressor = CompressorFactory::create_block_codec(compressor);

  boost::trim_right_if(m_log_dir, boost::is_any_of("/"));

  m_range_reference_required = props->get_bool("Hypertable.RangeServer.CommitLog.FragmentRemoval.RangeReferenceRequired");

  if (init_log) {
    if (m_range_reference_required)
      m_range_reference_required = init_log->range_reference_required();
    stitch_in(init_log);
    foreach (const CommitLogFileInfo *frag, m_fragment_queue) {
      if (frag->num >= m_cur_fragment_num)
        m_cur_fragment_num = frag->num + 1;
    }
  }
  else {  // chose one past the max one found in the directory
    uint32_t num;
    std::vector<String> listing;
    m_fs->readdir(m_log_dir, listing);
    for (size_t i=0; i<listing.size(); i++) {
      num = atoi(listing[i].c_str());
      if (num >= m_cur_fragment_num)
        m_cur_fragment_num = num + 1;
    }
  }

  if (m_range_reference_required)
    HT_INFOF("Range reference for '%s' is required", m_log_dir.c_str());
  else
    HT_INFOF("Range reference for '%s' is NOT required", m_log_dir.c_str());

  m_cur_fragment_fname = m_log_dir + "/" + m_cur_fragment_num;

  try {
    m_fs->mkdirs(m_log_dir);
    m_fd = m_fs->create(m_cur_fragment_fname, Filesystem::OPEN_FLAG_OVERWRITE,
                        -1, m_replication, -1);
  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("Problem initializing commit log '%s' - %s (%s)",
              m_log_dir.c_str(), e.what(), Error::get_text(e.code()));
    m_fd = -1;
    throw;
  }
}


int64_t CommitLog::get_timestamp() {
  ScopedLock lock(m_mutex);
  boost::xtime now;
  boost::xtime_get(&now, boost::TIME_UTC);
  return ((int64_t)now.sec * 1000000000LL) + (int64_t)now.nsec;
}

int
CommitLog::sync() {
  int error = Error::OK;

  // Sync commit log update (protected by lock)
  try {
    ScopedLock lock(m_mutex);
    if (m_fd == -1)
      return Error::CLOSED;
    m_fs->flush(m_fd);
    HT_DEBUG_OUT << "synced commit log explicitly" << HT_END;
  }
  catch (Exception &e) {
    HT_ERRORF("Problem syncing commit log: %s: %s",
              m_cur_fragment_fname.c_str(), e.what());
    error = e.code();
  }

  return error;
}

int CommitLog::write(DynamicBuffer &buffer, int64_t revision, bool sync) {
  int error;
  BlockCompressionHeaderCommitLog header(MAGIC_DATA, revision);

  if (m_needs_roll) {
    ScopedLock lock(m_mutex);
    if ((error = roll()) != Error::OK)
      return error;
  }

  /**
   * Compress and write the commit block
   */
  if ((error = compress_and_write(buffer, &header, revision, sync)) != Error::OK)
    return error;

  /**
   * Roll the log
   */
  if (m_cur_fragment_length > m_max_fragment_size) {
    ScopedLock lock(m_mutex);
    if ((error = roll()) != Error::OK)
      return error;
  }

  return Error::OK;
}


int CommitLog::link_log(CommitLogBase *log_base) {
  int error;
  int64_t link_revision = log_base->get_latest_revision();
  BlockCompressionHeaderCommitLog header(MAGIC_LINK, link_revision);

  DynamicBuffer input;
  String &log_dir = log_base->get_log_dir();

  if (m_linked_logs.count(md5_hash(log_dir.c_str())) > 0) {
    HT_WARNF("Skipping log %s because it is already linked in", log_dir.c_str());
    return Error::OK;
  }

  if (m_needs_roll) {
    ScopedLock lock(m_mutex);
    if ((error = roll()) != Error::OK)
      return error;
  }

  {
    ScopedLock lock(m_mutex);

    HT_INFOF("clgc Linking log %s into fragment %d; link_rev=%lld latest_rev=%lld",
             log_dir.c_str(), m_cur_fragment_num, (Lld)link_revision, (Lld)m_latest_revision);

    HT_ASSERT(link_revision > 0);

    if (link_revision > m_latest_revision)
      m_latest_revision = link_revision;
  }

  input.ensure(header.length());

  header.set_revision(link_revision);
  header.set_compression_type(BlockCompressionCodec::NONE);
  header.set_data_length(log_dir.length() + 1);
  header.set_data_zlength(log_dir.length() + 1);
  header.set_data_checksum(fletcher32(log_dir.c_str(), log_dir.length()+1));

  header.encode(&input.ptr);
  input.add(log_dir.c_str(), log_dir.length() + 1);

  try {
    ScopedLock lock(m_mutex);
    size_t amount = input.fill();
    StaticBuffer send_buf(input);
    CommitLogFileInfo *file_info;

    if (m_fd == -1)
      return Error::CLOSED;

    m_fs->append(m_fd, send_buf, false);
    m_cur_fragment_length += amount;

    if ((error = roll(&file_info)) != Error::OK)
      return error;

    file_info->purge_dirs.insert(log_dir);

    foreach (CommitLogFileInfo *fi, log_base->fragment_queue()) {
      if (fi->parent == 0) {
        fi->parent = file_info;
        file_info->references++;
      }
      m_fragment_queue.push_back(fi);
    }

  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("Problem linking external log into commit log - %s", e.what());
    return e.code();
  }

  m_linked_logs.insert(md5_hash(log_dir.c_str()));

  return Error::OK;
}


int CommitLog::close() {

  try {
    ScopedLock lock(m_mutex);
    if (m_fd > 0) {
      m_fs->close(m_fd);
      m_fd = -1;
    }
  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("Problem closing commit log file '%s' - %s (%s)",
              m_cur_fragment_fname.c_str(), e.what(),
              Error::get_text(e.code()));
    return e.code();
  }

  return Error::OK;
}


void CommitLog::remove_linked_log(const String &log_dir) {
  ScopedLock lock(m_mutex);
  int64_t log_dir_hash = md5_hash(log_dir.c_str());
  LogFragmentQueue::iterator iter = m_fragment_queue.begin();
  while (iter != m_fragment_queue.end()) {
    if ((*iter)->log_dir_hash == log_dir_hash) {
      // Decrement parent reference count
      if ((*iter)->parent) {
        HT_ASSERT((*iter)->parent->references > 0);
        (*iter)->parent->references--;
      }
      iter = m_fragment_queue.erase(iter);
    }
    else
      iter++;
  }
}


/**
 * The remove_ok set avoids a race condition by only allowing fragments of
 * transfer logs to be removed if the Ranges that own the transfer log
 * are live.  There was a situation where a transfer log got stitched into
 * the commit log, but the RS crashed before the corresponding Range was
 * loaded.  When the RS came up again, it removed the transfer log
 * prematurely, resulting in data loss.
 */
int CommitLog::purge(int64_t revision, std::set<int64_t> remove_ok) {
  ScopedLock lock(m_mutex);

  if (m_fd == -1)
    return Error::CLOSED;

  HT_DEBUG_OUT << "Purging commit log fragments with latest revision older than " << revision
              << HT_END;

  // Process "reap" set
  std::set<CommitLogFileInfo *>::iterator rm_iter, iter = m_reap_set.begin();
  while (iter != m_reap_set.end()) {
    if ((*iter)->references == 0) {
      remove_file_info(*iter);
      rm_iter = iter++;
      m_reap_set.erase(rm_iter);
    }
    else
      ++iter;
  }

  CommitLogFileInfo *fi;
  while (!m_fragment_queue.empty()) {
    fi = m_fragment_queue.front();
    if (fi->revision < revision &&
	(!m_range_reference_required || remove_ok.count(fi->log_dir_hash) > 0)) {

      if (fi->references == 0)
        remove_file_info(fi);
      else
        m_reap_set.insert(fi);

      m_fragment_queue.pop_front();
    }
    else {
      HT_DEBUGF("clgc LOG FRAGMENT PURGE breaking because %lld >= %lld",
               (Lld)fi->revision, (Lld)revision);
      break;
    }
  }

  return Error::OK;
}


void CommitLog::remove_file_info(CommitLogFileInfo *fi) {
  String fname;

  // Remove linked log directores
  foreach (const String &logdir, fi->purge_dirs) {
    try {
      m_fs->rmdir(logdir);
    }
    catch (Exception &e) {
      HT_ERRORF("Problem removing log directory '%s' (%s - %s)",
                logdir.c_str(), Error::get_text(e.code()), e.what());
    }
  }

  // Remove log fragment
  try {
    fname = fi->log_dir + "/" + fi->num;
    m_fs->remove(fname);
  }
  catch (Exception &e) {
    HT_ERRORF("Problem removing log fragment '%s' (%s - %s)",
              fname.c_str(), Error::get_text(e.code()), e.what());
  }

  HT_DEBUGF("clgc Removed log fragment file='%s' revision=%lld", fname.c_str(),
            (Lld)fi->revision);

  // Decrement parent reference count
  if (fi->parent) {
    HT_ASSERT(fi->parent->references > 0);
    fi->parent->references--;
  }

}


int CommitLog::roll(CommitLogFileInfo **clfip) {
  CommitLogFileInfo *file_info;

  if (m_fd == -1)
    return Error::CLOSED;

  if (m_latest_revision == TIMESTAMP_MIN)
    return Error::OK;

  m_needs_roll = true;

  if (m_fd > 0) {
    try {
      m_fs->close(m_fd);
    }
    catch (Exception &e) {
      HT_ERRORF("Problem closing commit log fragment: %s: %s",
		m_cur_fragment_fname.c_str(), e.what());
      return e.code();
    }

    m_fd = -1;

    file_info = new CommitLogFileInfo();
    if (clfip)
      *clfip = file_info;
    file_info->log_dir = m_log_dir;
    file_info->log_dir_hash = md5_hash(m_log_dir.c_str());
    file_info->num = m_cur_fragment_num;
    file_info->size = m_cur_fragment_length;
    assert(m_latest_revision != TIMESTAMP_MIN);
    file_info->revision = m_latest_revision;

    if (m_fragment_queue.empty() || m_fragment_queue.back()->revision
        < file_info->revision)
      m_fragment_queue.push_back(file_info);
    else {
      m_fragment_queue.push_back(file_info);
      struct LtClfip swo;
      sort(m_fragment_queue.begin(), m_fragment_queue.end(), swo);
    }

    m_latest_revision = TIMESTAMP_MIN;
    m_cur_fragment_length = 0;

    m_cur_fragment_num++;
    m_cur_fragment_fname = m_log_dir + "/" + m_cur_fragment_num;

  }

  try {
    m_fd = m_fs->create(m_cur_fragment_fname, Filesystem::OPEN_FLAG_OVERWRITE,
                        -1, m_replication, -1);
  }
  catch (Exception &e) {
    HT_ERRORF("Problem rolling commit log: %s: %s",
              m_cur_fragment_fname.c_str(), e.what());
    return e.code();
  }

  m_needs_roll = false;

  return Error::OK;
}


int
CommitLog::compress_and_write(DynamicBuffer &input,
    BlockCompressionHeader *header, int64_t revision, bool sync) {
  int error = Error::OK;
  DynamicBuffer zblock;

  // Compress block and kick off log write (protected by lock)
  try {
    ScopedLock lock(m_mutex);

    if (m_fd == -1)
      return Error::CLOSED;

    m_compressor->deflate(input, zblock, *header);

    size_t amount = zblock.fill();
    StaticBuffer send_buf(zblock);

    m_fs->append(m_fd, send_buf, sync);
    assert(revision != 0);
    if (revision > m_latest_revision)
      m_latest_revision = revision;
    m_cur_fragment_length += amount;
  }
  catch (Exception &e) {
    HT_ERRORF("Problem writing commit log: %s: %s",
              m_cur_fragment_fname.c_str(), e.what());
    error = e.code();
  }

  return error;
}


void CommitLog::load_cumulative_size_map(CumulativeSizeMap &cumulative_size_map) {
  ScopedLock lock(m_mutex);
  int64_t cumulative_total = 0;
  uint32_t distance = 0;
  CumulativeFragmentData frag_data;

  if (m_fd == -1)
    HT_THROWF(Error::CLOSED, "Commit log '%s' has been closed", m_log_dir.c_str());

  memset(&frag_data, 0, sizeof(frag_data));

  if (m_latest_revision != TIMESTAMP_MIN) {
    frag_data.size = m_cur_fragment_length;
    frag_data.fragno = m_cur_fragment_num;
    cumulative_size_map[m_latest_revision] = frag_data;
  }

  for (std::deque<CommitLogFileInfo *>::reverse_iterator iter
       = m_fragment_queue.rbegin(); iter != m_fragment_queue.rend(); ++iter) {
    frag_data.size = (*iter)->size;
    frag_data.fragno = (*iter)->num;
    cumulative_size_map[(*iter)->revision] = frag_data;
  }

  for (CumulativeSizeMap::reverse_iterator riter = cumulative_size_map.rbegin();
       riter != cumulative_size_map.rend(); riter++) {
    (*riter).second.distance = distance++;
    cumulative_total += (*riter).second.size;
    (*riter).second.cumulative_size = cumulative_total;
  }

}


void CommitLog::get_stats(const String &prefix, String &result) {
  ScopedLock lock(m_mutex);

  if (m_fd == -1)
    HT_THROWF(Error::CLOSED, "Commit log '%s' has been closed", m_log_dir.c_str());

  try {
    foreach (const CommitLogFileInfo *frag, m_fragment_queue) {
      result += prefix + String("-log-fragment[") + frag->num + "]\tsize\t" + frag->size + "\n";
      result += prefix + String("-log-fragment[") + frag->num + "]\trevision\t" + frag->revision + "\n";
      result += prefix + String("-log-fragment[") + frag->num + "]\tdir\t" + frag->log_dir + "\n";
    }
    result += prefix + String("-log-fragment[") + m_cur_fragment_num + "]\tsize\t" + m_cur_fragment_length + "\n";
    result += prefix + String("-log-fragment]") + m_cur_fragment_num + "]\trevision\t" + m_latest_revision + "\n";
    result += prefix + String("-log-fragment]") + m_cur_fragment_num + "]\tdir\t" + m_log_dir + "\n";
  }
  catch (Hypertable::Exception &e) {
    HT_ERROR_OUT << "Problem getting stats for log fragments" << HT_END;
    HT_THROW(e.code(), e.what());
  }
}

