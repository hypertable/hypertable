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

#ifndef FsBroker_mapr_MaprBroker_h
#define FsBroker_mapr_MaprBroker_h

#include <FsBroker/Lib/Broker.h>
#include <FsBroker/Lib/MetricsHandler.h>

#include <Common/Properties.h>
#include <Common/Status.h>
#include <Common/String.h>

#include <hdfs.h>

#include <atomic>
#include <string>

extern "C" {
#include <unistd.h>
}

namespace Hypertable {
namespace FsBroker {
  using namespace Lib;

  class OpenFileDataMapr : public OpenFileData {
  public:
  OpenFileDataMapr(hdfsFS _fs, const String &fname, hdfsFile _file, int _flags) 
    : fs(_fs), file(_file), flags(_flags), filename(fname) { }
    virtual ~OpenFileDataMapr() {
      if (hdfsCloseFile(fs, file) != 0) {
        HT_ERRORF("Error closing file '%s' - %s", filename.c_str(), strerror(errno));
      }
    }
    hdfsFS fs;
    hdfsFile file;
    int  flags;
    String filename;
  };

  class OpenFileDataMaprPtr : public OpenFileDataPtr {
  public:
    OpenFileDataMaprPtr() : OpenFileDataPtr() { }
    OpenFileDataMaprPtr(OpenFileDataMapr *ofdl)
      : OpenFileDataPtr(ofdl) { }
    OpenFileDataMapr *operator->() const {
      return (OpenFileDataMapr *)get();
    }
  };


  class MaprBroker : public FsBroker::Broker {
  public:
    MaprBroker(PropertiesPtr &props);
    virtual ~MaprBroker();

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

    /// Server status information
    Hypertable::Status m_status;

    bool         m_verbose;
	hdfsFS       m_filesystem;
	hdfsBuilder	*m_builder;
    bool         m_aggregate_writes;
    bool         m_readbuffering;
    String       m_namenode_host;
    uint16_t     m_namenode_port;
  };

}}

#endif // FsBroker_mapr_MaprBroker_h
