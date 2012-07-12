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

#ifndef HYPERTABLE_COMMITLOGBASE_H
#define HYPERTABLE_COMMITLOGBASE_H

#include <deque>

#include <boost/algorithm/string.hpp>

#include "Common/Mutex.h"
#include "Common/ReferenceCount.h"
#include "Common/String.h"
#include "Common/StringExt.h"

#include "KeySpec.h"
#include "CommitLogBlockStream.h"

namespace Hypertable {

  class CommitLogFileInfo {
  public:
    CommitLogFileInfo() : num(0), size(0), revision(0), log_dir_hash(0), references(0), block_stream(0), parent(0) { }
    String     log_dir;
    uint32_t   num;
    uint64_t   size;
    int64_t    revision;
    int64_t    log_dir_hash;
    size_t     references;
    CommitLogBlockStream *block_stream;
    CommitLogFileInfo *parent;
    StringSet  purge_dirs;
  };

  struct LtClfip {
    bool operator()(CommitLogFileInfo *x, CommitLogFileInfo *y) const {
      return x->revision < y->revision;
    }
  };

  typedef std::deque<CommitLogFileInfo *> LogFragmentQueue;

  /**
   */
  class CommitLogBase : public ReferenceCount {
  public:
    CommitLogBase(const String &log_dir)
      : m_log_dir(log_dir), m_latest_revision(TIMESTAMP_MIN), m_range_reference_required(true) {

      boost::trim_right_if(m_log_dir, boost::is_any_of("/"));
      
      size_t lastslash = m_log_dir.find_last_of('/');

      m_log_name = (lastslash == String::npos) ? m_log_dir
                                               : m_log_dir.substr(lastslash+1);
    }

    /**
     * This method assumes that the other commit log is not being
     * concurrently used which is why it doesn't lock it's mutex
     */
    void stitch_in(CommitLogBase *other) {
      ScopedLock lock(m_mutex);
      for (LogFragmentQueue::iterator iter = other->m_fragment_queue.begin();
           iter != other->m_fragment_queue.end(); iter++)
        m_fragment_queue.push_back(*iter);
    }

    String &get_log_dir() { return m_log_dir; }

    int64_t get_latest_revision() { return m_latest_revision; }

    bool empty() { ScopedLock lock(m_mutex); return m_fragment_queue.empty(); }

    std::set<int64_t> &get_linked_log_set() { return m_linked_logs; }

    bool range_reference_required() { return m_range_reference_required; }

    LogFragmentQueue &fragment_queue() { return m_fragment_queue; }

  protected:
    Mutex             m_mutex;
    String            m_log_dir;
    String            m_log_name;
    LogFragmentQueue  m_fragment_queue;
    int64_t           m_latest_revision;
    std::set<int64_t> m_linked_logs;
    bool m_range_reference_required;
  };

  typedef intrusive_ptr<CommitLogBase> CommitLogBasePtr;

} // namespace Hypertable

#endif // HYPERTABLE_COMMITLOGBASE_H

