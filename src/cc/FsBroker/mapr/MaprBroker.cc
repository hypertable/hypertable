/*
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

#include <Common/Compat.h>

#include "MaprBroker.h"

#include <AsyncComm/ReactorFactory.h>

#include <Common/FileUtils.h>
#include <Common/Filesystem.h>
#include <Common/Path.h>
#include <Common/ScopeGuard.h>
#include <Common/String.h>
#include <Common/System.h>
#include <Common/SystemInfo.h>

#include <boost/algorithm/string.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#if defined(__sun__)
#include <sys/fcntl.h>
#endif
#include <sys/uio.h>
#include <unistd.h>
}


// hdfs.h versions 
// #ifdef hdfsBuilderConnect
// #define HDFS_VER 275
// #elifdef hdfsConnectNewInstance
// #define HDFS_VER 021
// #else 
// #define HDFS_VER 0
// #endif

using namespace Hypertable;
using namespace Hypertable::FsBroker;
using namespace std;

atomic<int> MaprBroker::ms_next_fd {0};

MaprBroker::MaprBroker(PropertiesPtr &cfg) {
  m_verbose = cfg->get_bool("verbose");
  m_aggregate_writes = cfg->get_bool("DfsBroker.Mapr.aggregate.writes", true);
  m_readbuffering = cfg->get_bool("DfsBroker.Mapr.readbuffering", true);

  m_metrics_handler = std::make_shared<MetricsHandler>(cfg, "mapr");
  m_metrics_handler->start_collecting();

  m_builder = hdfsNewBuilder();
  //hdfsBuilderConfSetStr(m_builder, const char *key, const char *val);
  //m_namenode_host = cfg->get_str("DfsBroker.Hdfs.NameNode.Host");
  //m_namenode_port = cfg->get_i16("DfsBroker.Hdfs.NameNode.Port");
  hdfsBuilderSetNameNode(m_builder, "default");  //"default" > read from hadoop XML config from LIBHDFS3_CONF=path
  hdfsBuilderSetNameNodePort(m_builder, m_namenode_port);
  m_filesystem = hdfsBuilderConnect(m_builder);

  // m_filesystem = hdfsConnectNewInstance(m_namenode_host.c_str(), m_namenode_port);
}



MaprBroker::~MaprBroker() {
  m_metrics_handler->stop_collecting();
  hdfsDisconnect(m_filesystem);
}


void
MaprBroker::open(Response::Callback::Open *cb, const char *fname, 
		 uint32_t flags, uint32_t bufsz) {
  hdfsFile file;
  int fd;

  if (bufsz == (uint32_t)-1)
    bufsz = 0;

  HT_DEBUGF("open file='%s' flags=%u bufsz=%d", fname, flags, bufsz);

  fd = ++ms_next_fd;

  int oflags = O_RDONLY;

  if ((file = hdfsOpenFile(m_filesystem, fname, oflags, bufsz, 0, 0)) == 0) {
    report_error(cb);
    HT_ERRORF("open failed: file='%s' - %s", fname, strerror(errno));
    return;
  }

  HT_INFOF("open(%s) = %d", fname, (int)fd);

  {
    struct sockaddr_in addr;
    OpenFileDataMaprPtr fdata(new OpenFileDataMapr(m_filesystem, fname, file, O_RDONLY));

    cb->get_address(addr);

    m_open_file_map.create(fd, addr, fdata);

    cb->response(fd);
  }
}


void
MaprBroker::create(Response::Callback::Open *cb, const char *fname, uint32_t flags,
                    int32_t bufsz, int16_t replication, int64_t blksz) {
  hdfsFile file;
  int fd;
  int oflags = O_WRONLY;

  if (bufsz == -1)
    bufsz = 0;
  if (replication == -1)
    replication = 0;
  if (blksz == -1)
    blksz = 0;

  HT_DEBUGF("create file='%s' flags=%u bufsz=%d replication=%d blksz=%lld",
            fname, flags, bufsz, (int)replication, (Lld)blksz);

  fd = ++ms_next_fd;

  if ((flags & Filesystem::OPEN_FLAG_OVERWRITE) == 0)
    oflags |= O_APPEND;

  if ((file = hdfsOpenFile(m_filesystem, fname, oflags, bufsz, replication, blksz)) == 0) {
    report_error(cb);
    HT_ERRORF("create failed: file='%s' - %s", fname, strerror(errno));
    return;
  }

  //HT_DEBUGF("created file='%s' fd=%d mapr_fd=%d", fname, fd, mapr_fd);

  HT_INFOF("create(%s) = %d", fname, (int)fd);

  {
    struct sockaddr_in addr;
    OpenFileDataMaprPtr fdata(new OpenFileDataMapr(m_filesystem, fname, file, oflags));

    cb->get_address(addr);

    m_open_file_map.create(fd, addr, fdata);

    cb->response(fd);
  }
}


void MaprBroker::close(ResponseCallback *cb, uint32_t fd) {
  int error;
  HT_DEBUGF("close fd=%d", fd);
  m_open_file_map.remove(fd);
  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for close(%u) - %s", (unsigned)fd, Error::get_text(error));
}


void MaprBroker::read(Response::Callback::Read *cb, uint32_t fd, uint32_t amount) {
  OpenFileDataMaprPtr fdata;
  tSize nread;
  int64_t offset;
  uint8_t *readbuf;
  int error;

#if defined(__linux__)
  void *vptr = 0;
  HT_ASSERT(posix_memalign(&vptr, HT_DIRECT_IO_ALIGNMENT, amount) == 0);
  readbuf = (uint8_t *)vptr;
#else
  readbuf = new uint8_t [amount];
#endif

  StaticBuffer buf(readbuf, amount);

  HT_DEBUGF("read fd=%d amount=%d", fd, amount);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    HT_ERRORF("bad file handle: %d", fd);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((offset = hdfsTell(m_filesystem, fdata->file)) == -1) {
    report_error(cb);
    HT_ERRORF("lseek failed: fd=%d offset=0 SEEK_CUR - %s", fd,
              strerror(errno));
    return;
  }

  if ((nread = hdfsRead(m_filesystem, fdata->file, buf.base, (tSize)amount)) == -1) {
    report_error(cb);
    HT_ERRORF("read failed: fd=%d offset=%llu amount=%d - %s",
	      fd, (Llu)offset, amount, strerror(errno));
    return;
  }

  buf.size = nread;

  m_metrics_handler->add_bytes_read(nread);

  if ((error = cb->response(offset, buf)) != Error::OK)
    HT_ERRORF("Problem sending response for read(%u, %u) - %s",
              (unsigned)fd, (unsigned)amount, Error::get_text(error));

}


void MaprBroker::append(Response::Callback::Append *cb, uint32_t fd,
                        uint32_t amount, const void *data, Filesystem::Flags flags) {
  OpenFileDataMaprPtr fdata;
  tSize nwritten;
  int64_t offset;
  int error;

  HT_DEBUG_OUT << "append fd=" << fd << " amount=" << amount << " data='"
               << format_bytes(20, data, amount) << " flags="
               << static_cast<int>(flags) << HT_END;

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((offset = hdfsTell(m_filesystem, fdata->file)) == -1) {
    report_error(cb);
    HT_ERRORF("lseek failed: fd=%d offset=0 SEEK_CUR - %s", fd,
              strerror(errno));
    return;
  }

  if ((nwritten = hdfsWrite(m_filesystem, fdata->file, data, (tSize)amount)) == -1) {
    report_error(cb);
    HT_ERRORF("write failed: fd=%d offset=%llu amount=%d data=%p- %s",
	      fd, (Llu)offset, amount, data, strerror(errno));
    return;
  }

  if (flags == Filesystem::Flags::FLUSH || flags == Filesystem::Flags::SYNC) {
    int64_t start_time = get_ts64();
    if (hdfsFlush(m_filesystem, fdata->file)) {
      report_error(cb);
      HT_ERRORF("flush failed: fd=%d - %s", fd, strerror(errno));
      return;
    }
    m_metrics_handler->add_sync(get_ts64() - start_time);
  }

  m_metrics_handler->add_bytes_written(nwritten);

  if ((error = cb->response(offset, nwritten)) != Error::OK)
    HT_ERRORF("Problem sending response for append(%u, %u) - %s",
              (unsigned)fd, (unsigned)amount, Error::get_text(error));

}


void MaprBroker::seek(ResponseCallback *cb, uint32_t fd, uint64_t offset) {
  OpenFileDataMaprPtr fdata;
  int error;

  HT_DEBUGF("seek fd=%lu offset=%llu", (Lu)fd, (Llu)offset);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((offset = hdfsSeek(m_filesystem, fdata->file, (tOffset)offset)) == (uint64_t)-1) {
    report_error(cb);
    HT_ERRORF("lseek failed: fd=%d offset=%llu - %s", fd, (Llu)offset,
              strerror(errno));
    return;
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for seek(%u, %llu) - %s",
              (unsigned)fd, (Llu)offset, Error::get_text(error));

}


void MaprBroker::remove(ResponseCallback *cb, const char *fname) {
  int error;

  HT_DEBUGF("remove file='%s'", fname);

  if (hdfsDelete(m_filesystem, fname, 1) == -1) {
    report_error(cb);
    HT_ERRORF("unlink failed: file='%s' - %s", fname,
              strerror(errno));
    return;
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for remove(%s) - %s",
              fname, Error::get_text(error));
}


void MaprBroker::length(Response::Callback::Length *cb, const char *fname,
                        bool accurate) {
  hdfsFileInfo *fileInfo;
  int error;

  (void)accurate;

  if ((fileInfo = hdfsGetPathInfo(m_filesystem, fname)) == 0) {
    report_error(cb);
    HT_ERRORF("length (stat) failed: file='%s' - %s", fname,
              strerror(errno));
    return;
  }

  HT_DEBUGF("length('%s') = %lu", fname, (unsigned long)fileInfo->mSize);
  
  if ((error = cb->response(fileInfo->mSize)) != Error::OK)
    HT_ERRORF("Problem sending response (%llu) for length(%s) - %s",
              (Llu)fileInfo->mSize, fname, Error::get_text(error));

  hdfsFreeFileInfo(fileInfo, 1);
}


void
MaprBroker::pread(Response::Callback::Read *cb, uint32_t fd, uint64_t offset,
		  uint32_t amount, bool) {
  OpenFileDataMaprPtr fdata;
  ssize_t nread;
  uint8_t *readbuf;
  int error;

  HT_DEBUGF("pread fd=%d offset=%llu amount=%d", fd, (Llu)offset, amount);

#if defined(__linux__)
  void *vptr = 0;
  HT_ASSERT(posix_memalign(&vptr, HT_DIRECT_IO_ALIGNMENT, amount) == 0);
  readbuf = (uint8_t *)vptr;
#else
  readbuf = new uint8_t [amount];
#endif

  StaticBuffer buf(readbuf, amount);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((nread = hdfsPread(m_filesystem, fdata->file, (tOffset)offset, buf.base, (tSize)amount)) == -1) {
    report_error(cb);
    HT_ERRORF("pread failed: fd=%d amount=%d offset=%llu - %s", fd,
              amount, (Llu)offset, strerror(errno));
    return;
  }

  buf.size = nread;

  m_metrics_handler->add_bytes_read(nread);

  if ((error = cb->response(offset, buf)) != Error::OK)
    HT_ERRORF("Problem sending response for pread(%u, %llu, %u) - %s",
              (unsigned)fd, (Llu)offset, (unsigned)amount, Error::get_text(error));

}


void MaprBroker::mkdirs(ResponseCallback *cb, const char *dname) {
  int error;

  HT_DEBUGF("mkdirs dir='%s'", dname);

  String make_dir = dname;

  boost::trim_right_if(make_dir, boost::is_any_of("/"));

  if (hdfsCreateDirectory(m_filesystem, make_dir.c_str()) == -1) {
    report_error(cb);
    HT_ERRORF("mkdirs failed: dname='%s' - %s", dname,
              strerror(errno));
    return;
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for mkdirs(%s) - %s",
              dname, Error::get_text(error));
}

namespace {

  void free_file_info(hdfsFileInfo *fileInfo, int numEntries) {
    if (numEntries != 0)
      hdfsFreeFileInfo(fileInfo, numEntries);
  }


  void rmdir_recursive(hdfsFS fs, const String &dname) {
    hdfsFileInfo *fileInfo = 0;
    int numEntries = 0;

    HT_ON_SCOPE_EXIT(&free_file_info, fileInfo, numEntries);

    if ((fileInfo = hdfsListDirectory(fs, dname.c_str(), &numEntries)) == 0) {
      if (errno == ENOENT)
	return;
      HT_THROWF(Error::FSBROKER_IO_ERROR, "Problem listing directory '%s' - %s",
		dname.c_str(), strerror(errno));
    }

    for (int i=0; i<numEntries; i++) {
      String child = fileInfo[i].mName;
      if (fileInfo[i].mKind == kObjectKindDirectory)
	rmdir_recursive(fs, child);
      else if (fileInfo[i].mKind == kObjectKindFile) {
	    if (hdfsDelete(fs, child.c_str(), 1) == -1) {
	      if (errno != 0) {
	         HT_THROWF(Error::FSBROKER_IO_ERROR, "Problem deleting file '%s' - %s",
		               child.c_str(), strerror(errno));
	      }
	    }
      }
    }
	if (hdfsDelete(fs, dname.c_str(), 1) == -1) {
      HT_THROWF(Error::FSBROKER_IO_ERROR, "Problem removing directory '%s' - %s",
		dname.c_str(), strerror(errno));
    }
  }

}

void MaprBroker::rmdir(ResponseCallback *cb, const char *dname) {
  int error;
  String removal_dir = String(dname);

  boost::trim_right_if(removal_dir, boost::is_any_of("/"));

  if (m_verbose)
    HT_DEBUGF("rmdir dir='%s'", dname);

  try {
    rmdir_recursive(m_filesystem, removal_dir);
  }
  catch (Hypertable::Exception &e) {
    if ((error = cb->error(e.code(), e.what())) != Error::OK)
      HT_ERRORF("Problem sending error response for rmdir(%s) - %s",
		dname, Error::get_text(error));
    HT_ERROR_OUT << e << HT_END;
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for mkdirs(%s) - %s",
              dname, Error::get_text(error));
}


void MaprBroker::readdir(Response::Callback::Readdir *cb, const char *dname) {
  std::vector<Filesystem::Dirent> listing;
  hdfsFileInfo *fileInfo;
  int numEntries;

  HT_DEBUGF("Readdir dir='%s'", dname);

  if ((fileInfo = hdfsListDirectory(m_filesystem, dname, &numEntries)) == 0) {
    report_error(cb);
    HT_ERRORF("readdir('%s') failed - %s", dname, strerror(errno));
    return;
  }

  Filesystem::Dirent entry;
  for (int i=0; i<numEntries; i++) {
    const char *ptr;
    if (fileInfo[i].mName[0] != '.' && fileInfo[i].mName[0] != 0) {
      if ((ptr = strrchr(fileInfo[i].mName, '/')))
	entry.name = (String)(ptr+1);
      else
	entry.name = (String)fileInfo[i].mName;
      entry.length = fileInfo[i].mSize;
      entry.last_modification_time = fileInfo[i].mLastMod;
      entry.is_dir = fileInfo[i].mKind == kObjectKindDirectory;
      listing.push_back(entry);
    }
  }
  hdfsFreeFileInfo(fileInfo, numEntries);

  HT_DEBUGF("Sending back %d listings", (int)listing.size());

  cb->response(listing);
}


void MaprBroker::flush(ResponseCallback *cb, uint32_t fd) {
  this->sync(cb, fd);
}

void MaprBroker::sync(ResponseCallback *cb, uint32_t fd) {
  OpenFileDataMaprPtr fdata;

  HT_DEBUGF("sync fd=%d", fd);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  HT_DEBUGF("sync fd=%d filename=%s", fd, fdata->filename.c_str());

  int64_t start_time = get_ts64();
  if (hdfsFlush(m_filesystem, fdata->file) == -1) {
    report_error(cb);
    HT_ERRORF("sync failed: fd=%d - %s", fd, strerror(errno));
    return;
  }
  m_metrics_handler->add_sync(get_ts64() - start_time);

  cb->response_ok();
}


void MaprBroker::status(Response::Callback::Status *cb) {
  cb->response(m_status);
}


void MaprBroker::shutdown(ResponseCallback *cb) {
  m_open_file_map.remove_all();
  cb->response_ok();
  this_thread::sleep_for(chrono::milliseconds(2000));
}


void MaprBroker::exists(Response::Callback::Exists *cb, const char *fname) {
  String abspath;

  HT_DEBUGF("exists file='%s'", fname);

  if (hdfsExists(m_filesystem, fname) == -1) {
    cb->response(false);
    return;
  }

  cb->response(true);
}


void
MaprBroker::rename(ResponseCallback *cb, const char *src, const char *dst) {

  HT_DEBUGF("rename %s -> %s", src, dst);

  if (hdfsRename(m_filesystem, src, dst) == -1) {
    report_error(cb);
    return;
  }
  cb->response_ok();
}

void
MaprBroker::debug(ResponseCallback *cb, int32_t command,
                   StaticBuffer &serialized_parameters) {
  HT_ERRORF("debug command %d not implemented.", command);
  cb->error(Error::NOT_IMPLEMENTED, format("Unsupported debug command - %d",
            command));
}



void MaprBroker::report_error(ResponseCallback *cb) {
  char errbuf[128];
  errbuf[0] = 0;

  m_metrics_handler->increment_error_count();

  strerror_r(errno, errbuf, 128);

  if (errno == ENOTDIR || errno == ENAMETOOLONG || errno == ENOENT)
    cb->error(Error::FSBROKER_BAD_FILENAME, errbuf);
  else if (errno == EACCES || errno == EPERM)
    cb->error(Error::FSBROKER_PERMISSION_DENIED, errbuf);
  else if (errno == EBADF)
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
  else if (errno == EINVAL)
    cb->error(Error::FSBROKER_INVALID_ARGUMENT, errbuf);
  else
    cb->error(Error::FSBROKER_IO_ERROR, errbuf);
}
