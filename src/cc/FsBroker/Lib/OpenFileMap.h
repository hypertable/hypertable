/* -*- c++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
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

#ifndef FsBroker_Lib_OpenFileMap_h
#define FsBroker_Lib_OpenFileMap_h

#include <Common/Logger.h>

#include <memory>
#include <mutex>
#include <unordered_map>

extern "C" {
#include <netinet/in.h>
}

namespace Hypertable {
namespace FsBroker {
namespace Lib {

  /// @addtogroup FsBrokerLib
  /// @{

  class OpenFileData {
  public:
    virtual ~OpenFileData() { return; }
    struct sockaddr_in addr;
  };

  typedef std::shared_ptr<OpenFileData> OpenFileDataPtr;


  class OpenFileMap {

  public:
	  
    void create(int fd, struct sockaddr_in &addr, OpenFileDataPtr &fdata) {
      std::lock_guard<std::mutex> lock(m_mutex);
      fdata->addr = addr;
      m_file_map[fd] = fdata;
    }

	void create(int fd, OpenFileDataPtr  &fdata) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_file_map[fd] = fdata;
	}

    bool get(int fd, OpenFileDataPtr &fdata) {
      std::lock_guard<std::mutex> lock(m_mutex);
      FileMap::iterator iter = m_file_map.find(fd);
      if (iter != m_file_map.end()) {
        fdata = (*iter).second;
        return true;
      }
      return false;
    }

    bool remove(int fd, OpenFileDataPtr &fdata) {
      std::lock_guard<std::mutex> lock(m_mutex);
      FileMap::iterator iter = m_file_map.find(fd);
      if (iter != m_file_map.end()) {
        fdata = (*iter).second;
        m_file_map.erase(iter);
        return true;
      }
      return false;
    }

    void remove(int fd) {
      std::lock_guard<std::mutex> lock(m_mutex);
      FileMap::iterator iter = m_file_map.find(fd);
      if (iter != m_file_map.end())
        m_file_map.erase(iter);
    }

    void remove_all(struct sockaddr_in &addr) {
      std::lock_guard<std::mutex> lock(m_mutex);
      FileMap::iterator iter = m_file_map.begin();

      while (iter != m_file_map.end()) {
        if ((*iter).second->addr.sin_family == addr.sin_family &&
            (*iter).second->addr.sin_port == addr.sin_port &&
            (*iter).second->addr.sin_addr.s_addr == addr.sin_addr.s_addr) {
          FileMap::iterator del_it = iter;
          HT_INFOF("Removing handle %d from open file map because of lost "
                   "owning client connection", (*iter).first);
          ++iter;
          m_file_map.erase(del_it);
        }
        else
          ++iter;
      }
    }

    void remove_all() {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_file_map.clear();
    }

  private:

    typedef std::unordered_map<int, OpenFileDataPtr> FileMap;

    std::mutex m_mutex;
    FileMap m_file_map;
  };

  /// @}

}}}

#endif // FsBroker_Lib_OpenFileMap_h