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

#ifndef FsBroker_local_LocalBroker_h
#define FsBroker_local_LocalBroker_h

#include <FsBroker/Lib/Broker.h>
#include <FsBroker/Lib/MetricsHandler.h>
#include <FsBroker/Lib/StatusManager.h>

#include <Common/Properties.h>
#include <Common/String.h>

#include <atomic>
#include <string>

extern "C" {
#include <unistd.h>
}

namespace Hypertable {
namespace FsBroker {

  using namespace Lib;

  class OpenFileDataLocal : public OpenFileData {
  public:
  OpenFileDataLocal(const String &fname, int _fd, int _flags) : fd(_fd), flags(_flags), filename(fname) { }
    virtual ~OpenFileDataLocal() {
      close(fd);
    }
    int  fd;
    int  flags;
    String filename;
  };

  class OpenFileDataLocalPtr : public OpenFileDataPtr {
  public:
    OpenFileDataLocalPtr() : OpenFileDataPtr() { }
    OpenFileDataLocalPtr(OpenFileDataLocal *ofdl)
      : OpenFileDataPtr(ofdl) { }
    OpenFileDataLocal *operator->() const {
      return (OpenFileDataLocal *)get();
    }
  };

  class LocalBroker : public Lib::Broker {
  public:
    LocalBroker(PropertiesPtr &props);
    virtual ~LocalBroker();

    virtual void open(Response::Callback::Open *cb, const char *fname,
                      uint32_t flags, uint32_t bufsz);
    virtual void create(Response::Callback::Open *cb, const char *fname, uint32_t flags,
           int32_t bufsz, int16_t replication, int64_t blksz);
    virtual void close(ResponseCallback *cb, uint32_t fd);
    virtual void read(Response::Callback::Read *cb, uint32_t fd, uint32_t amount);
    virtual void append(Response::Callback::Append *cb, uint32_t fd,
                        uint32_t amount, const void *data, Filesystem::Flags flags);
    virtual void seek(ResponseCallback *cb, uint32_t fd, uint64_t offset);
    virtual void remove(ResponseCallback *cb, const char *fname);
    virtual void length(Response::Callback::Length *cb, const char *fname,
                    bool accurate = true);
    virtual void pread(Response::Callback::Read *cb, uint32_t fd, uint64_t offset,
                       uint32_t amount, bool verify_checksum);
    virtual void mkdirs(ResponseCallback *cb, const char *dname);
    virtual void rmdir(ResponseCallback *cb, const char *dname);
    virtual void readdir(Response::Callback::Readdir *cb, const char *dname);
    virtual void flush(ResponseCallback *cb, uint32_t fd);
    virtual void sync(ResponseCallback *cb, uint32_t fd);
    virtual void status(Response::Callback::Status *cb);
    virtual void shutdown(ResponseCallback *cb);
    virtual void exists(Response::Callback::Exists *cb, const char *fname);
    virtual void rename(ResponseCallback *cb, const char *src, const char *dst);
    virtual void debug(ResponseCallback *, int32_t command,
                       StaticBuffer &serialized_parameters);

  private:

    static atomic<int> ms_next_fd;

    virtual void report_error(ResponseCallback *cb);

    /// Metrics collection handler
    MetricsHandlerPtr m_metrics_handler;

    /// Server status manager
    StatusManager m_status_manager;

    String m_rootdir;
    gBoolPtr m_verbose;
    bool m_directio;
    bool m_no_removal;
  };

}}

#endif // FsBroker_local_LocalBroker_h
