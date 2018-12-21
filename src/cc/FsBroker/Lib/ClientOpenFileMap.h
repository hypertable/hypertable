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

#ifndef FsBroker_Lib_ClientOpenFileMap_h
#define FsBroker_Lib_ClientOpenFileMap_h

#include <Common/Logger.h>
#include <FsBroker/Lib/ClientBufferedReaderHandler.h>

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

  class ClientOpenFileData {
  public:
	  ClientOpenFileData(const String &fname, int _flags, uint64_t _pos = 0) : name(fname), flags(_flags), pos(_pos) { }
	  void add_offset(uint64_t offset) {
		  pos += offset;
	  }	  
	  void deduct_offset(uint64_t offset) {
		  pos -= offset;
	  }
	  void set_position(uint64_t n_pos) {
		  pos = n_pos;
	  }
	  void set_position(uint64_t n_pos, size_t len) {
		  pos = n_pos + len;
	  }
	  virtual ~ClientOpenFileData() { }
	  String name;
	  uint32_t flags;
	  uint64_t pos;
	  ClientBufferedReaderHandler *buff_reader;
  };
  typedef std::shared_ptr<ClientOpenFileData> ClientOpenFileDataPtr;

  class ClientOpenFileMap {
  public:
	  void add(int fd, const String &name, int flags, uint64_t pos = 0) {
		ClientOpenFileDataPtr ofd(new ClientOpenFileData(name, flags, pos));
		create(fd, ofd);
	  }
	  void add(int fd, const String &name, int flags, ClientBufferedReaderHandler *buff_reader) {
		ClientOpenFileDataPtr ofd(new ClientOpenFileData(name, flags, buff_reader->get_position()));
		ofd->buff_reader = buff_reader;
		create(fd, ofd);
	  }
	  void create(int fd, ClientOpenFileDataPtr &ofd) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_file_map[fd] = ofd;
		HT_INFOF("ClientOpenFileMap-create fd %d: %s", fd, ofd->name.c_str());
	  }
	  bool exists(int fd) {
		HT_INFOF("ClientOpenFileMap-exists fd %d", fd);
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_file_map.find(fd) != m_file_map.end();
	  }
	  bool get(int fd, ClientOpenFileDataPtr &ofd) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_file_map.find(fd) != m_file_map.end()) {
			HT_INFOF("ClientOpenFileMap-get fd %d false", fd);
			return false;
		}
		ofd = m_file_map[fd];
		HT_INFOF("ClientOpenFileMap-get fd %d: %s", fd, ofd->name.c_str());
		return true;
	  }
	  bool not_buffered(int fd) {
		  std::lock_guard<std::mutex> lock(m_mutex);
		  if (m_file_map.find(fd) == m_file_map.end()) return true;
		  HT_INFOF("ClientOpenFileMap-not_buffered fd %d: %s", fd, m_file_map[fd]->name.c_str());
		  return (m_file_map[fd]->buff_reader?false:true);
	  }
	  
	  bool popto(int fd, ClientOpenFileDataPtr &ofd) {
		  std::lock_guard<std::mutex> lock(m_mutex);
		  if (m_file_map.find(fd) != m_file_map.end()) {
			  HT_INFOF("ClientOpenFileMap-popto fd %d false", fd);
			  return false;
		  }
		  ofd = m_file_map[fd];
		  HT_INFOF("ClientOpenFileMap-get fd %d: %s", fd, ofd->name.c_str());
		  return true;
	  }
	  void remove(int fd) {
		  HT_INFOF("ClientOpenFileMap-remove fd %d", fd);
		  if (!exists(fd))return;
		  HT_INFOF("ClientOpenFileMap-remove fd %d true", fd);
		  std::lock_guard<std::mutex> lock(m_mutex);
		  m_file_map.erase(fd);
	  }
	  void remove_all() {
		  std::lock_guard<std::mutex> lock(m_mutex);
		  m_file_map.clear();
	  }
  private:
	  typedef std::unordered_map<int, ClientOpenFileDataPtr> FileMap;
	  FileMap m_file_map;
	  std::mutex m_mutex;
  };

  /// @}

}}}

#endif // FsBroker_Lib_ClientOpenFileMap_h
