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

#include "LocalBroker.h"

#include <Common/FileUtils.h>
#include <Common/Filesystem.h>
#include <Common/Path.h>
#include <Common/String.h>
#include <Common/System.h>
#include <Common/SystemInfo.h>

#include <AsyncComm/ReactorFactory.h>

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
#if defined(__sun__)
#include <sys/fcntl.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
}

using namespace Hypertable;
using namespace Hypertable::FsBroker;
using namespace std;

atomic<int> LocalBroker::ms_next_fd {0};

LocalBroker::LocalBroker(PropertiesPtr &props) {
  m_verbose = props->get_ptr<gBool>("verbose");
  m_no_removal = props->get_bool("FsBroker.DisableFileRemoval");
  m_directio = props->get_bool("FsBroker.Local.DirectIO");

  m_metrics_handler = std::make_shared<MetricsHandler>(props, "local");
  m_metrics_handler->start_collecting();

#if defined(__linux__)
  // disable direct i/o for kernels < 2.6
  if (m_directio) {
    if (System::os_info().version_major == 2 &&
        System::os_info().version_minor < 6)
      m_directio = false;
  }
#endif

  /**
   * Determine root directory
   */
  Path root = Path(props->get_str("root", ""));

  if (!root.is_complete()) {
    Path data_dir = props->get_str("Hypertable.DataDirectory");
    root = data_dir / root;
  }

  m_rootdir = root.string();

  // ensure that root directory exists
  if (!FileUtils::mkdirs(m_rootdir))
    exit(EXIT_FAILURE);
}



LocalBroker::~LocalBroker() {
  m_metrics_handler->stop_collecting();
}


void
LocalBroker::open(Response::Callback::Open *cb, const char *fname, 
                  uint32_t flags, uint32_t bufsz) {
  int fd, local_fd;
  String abspath;

  HT_DEBUGF("open file='%s' flags=%u bufsz=%d", fname, flags, bufsz);

  if (fname[0] == '/')
    abspath = m_rootdir + fname;
  else
    abspath = m_rootdir + "/" + fname;

  fd = ++ms_next_fd;

  int oflags = O_RDONLY;

  if (m_directio && flags & Filesystem::OPEN_FLAG_DIRECTIO) {
#ifdef O_DIRECT
    oflags |= O_DIRECT;
#endif
  }

  /**
   * Open the file
   */
  if ((local_fd = ::open(abspath.c_str(), oflags)) == -1) {
    report_error(cb);
    HT_ERRORF("open failed: file='%s' - %s", abspath.c_str(), strerror(errno));
    return;
  }

#if defined(__APPLE__)
#ifdef F_NOCACHE
  //fcntl(local_fd, F_NOCACHE, 1);
#endif  
#elif defined(__sun__)
  if (m_directio)
    directio(local_fd, DIRECTIO_ON);
#endif

  HT_INFOF("open( %s ) = %d (local=%d)", fname, (int)fd, local_fd);

  {
    struct sockaddr_in addr;
    OpenFileDataLocalPtr fdata(new OpenFileDataLocal(fname, local_fd, O_RDONLY));

    cb->get_address(addr);

    m_open_file_map.create(fd, addr, fdata);

    cb->response(fd);
  }
}


void
LocalBroker::create(Response::Callback::Open *cb, const char *fname, uint32_t flags,
                    int32_t bufsz, int16_t replication, int64_t blksz) {
  int fd, local_fd;
  int oflags = O_WRONLY | O_CREAT;
  String abspath;

  HT_DEBUGF("create file='%s' flags=%u bufsz=%d replication=%d blksz=%lld",
            fname, flags, bufsz, (int)replication, (Lld)blksz);

  if (fname[0] == '/')
    abspath = m_rootdir + fname;
  else
    abspath = m_rootdir + "/" + fname;

  fd = ++ms_next_fd;

  if (flags & Filesystem::OPEN_FLAG_OVERWRITE)
    oflags |= O_TRUNC;
  else
    oflags |= O_APPEND;

  if (m_directio && flags & Filesystem::OPEN_FLAG_DIRECTIO) {
#ifdef O_DIRECT
    oflags |= O_DIRECT;
#endif
  }

  /**
   * Open the file
   */
  if ((local_fd = ::open(abspath.c_str(), oflags, 0644)) == -1) {
    report_error(cb);
    HT_ERRORF("open failed: file='%s' - %s", abspath.c_str(), strerror(errno));
    return;
  }

#if defined(__APPLE__)
#ifdef F_NOCACHE
    fcntl(local_fd, F_NOCACHE, 1);
#endif  
#elif defined(__sun__)
    if (m_directio)
      directio(local_fd, DIRECTIO_ON);
#endif

  //HT_DEBUGF("created file='%s' fd=%d local_fd=%d", fname, fd, local_fd);

  HT_INFOF("create( %s ) = %d (local=%d)", fname, (int)fd, local_fd);

  {
    struct sockaddr_in addr;
    OpenFileDataLocalPtr fdata(new OpenFileDataLocal(fname, local_fd, O_WRONLY));

    cb->get_address(addr);

    m_open_file_map.create(fd, addr, fdata);

    cb->response(fd);
  }
}


void LocalBroker::close(ResponseCallback *cb, uint32_t fd) {
  int error;
  HT_DEBUGF("close fd=%d", fd);
  m_open_file_map.remove(fd);
  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for close(%u) - %s", (unsigned)fd, Error::get_text(error));
}


void LocalBroker::read(Response::Callback::Read *cb, uint32_t fd, uint32_t amount) {
  OpenFileDataLocalPtr fdata;
  ssize_t nread;
  uint64_t offset;
  int error;

  StaticBuffer buf((size_t)amount, (size_t)HT_DIRECT_IO_ALIGNMENT);

  HT_DEBUGF("read fd=%d amount=%d", fd, amount);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    HT_ERRORF("bad file handle: %d", fd);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((offset = (uint64_t)lseek(fdata->fd, 0, SEEK_CUR)) == (uint64_t)-1) {
    int error = errno;
    report_error(cb);
    if (error != EINVAL)
      m_status_manager.set_read_error(error);
    HT_ERRORF("lseek failed: fd=%d offset=0 SEEK_CUR - %s", fdata->fd,
              strerror(errno));
    return;
  }

  if ((nread = FileUtils::read(fdata->fd, buf.base, amount)) == -1) {
    int error = errno;
    report_error(cb);
    m_status_manager.set_read_error(error);
    HT_ERRORF("read failed: fd=%d offset=%llu amount=%d - %s",
	      fdata->fd, (Llu)offset, amount, strerror(errno));
    return;
  }

  buf.size = nread;
  m_metrics_handler->add_bytes_read(buf.size);

  m_status_manager.clear_status();

  if ((error = cb->response(offset, buf)) != Error::OK)
    HT_ERRORF("Problem sending response for read(%u, %u) - %s",
              (unsigned)fd, (unsigned)amount, Error::get_text(error));

}


void LocalBroker::append(Response::Callback::Append *cb, uint32_t fd,
                         uint32_t amount, const void *data, Filesystem::Flags flags) {
  OpenFileDataLocalPtr fdata;
  ssize_t nwritten;
  uint64_t offset;
  int error;

  HT_DEBUG_OUT << "append fd=" << fd << " amount=" << amount << " data='"
               << format_bytes(20, data, amount) << " flags="
               << static_cast<uint8_t>(flags) << HT_END;

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((offset = (uint64_t)lseek(fdata->fd, 0, SEEK_CUR)) == (uint64_t)-1) {
    int error = errno;
    report_error(cb);
    if (error != EINVAL)
      m_status_manager.set_write_error(error);
    HT_ERRORF("lseek failed: fd=%d offset=0 SEEK_CUR - %s", fdata->fd,
              strerror(errno));
    return;
  }

  if ((nwritten = FileUtils::write(fdata->fd, data, amount)) == -1) {
    int error = errno;
    report_error(cb);
    m_status_manager.set_write_error(error);
    HT_ERRORF("write failed: fd=%d offset=%llu amount=%d data=%p- %s",
	      fdata->fd, (Llu)offset, amount, data, strerror(errno));
    return;
  }

  if (flags == Filesystem::Flags::FLUSH || flags == Filesystem::Flags::SYNC) {
    int64_t start_time = get_ts64();
    if (fsync(fdata->fd) != 0) {
      int error = errno;
      report_error(cb);
      m_status_manager.set_write_error(error);
      HT_ERRORF("flush failed: fd=%d - %s", fdata->fd, strerror(errno));
      return;
    }
    m_metrics_handler->add_sync(get_ts64() - start_time);
  }

  m_metrics_handler->add_bytes_written(nwritten);
  m_status_manager.clear_status();

  if ((error = cb->response(offset, nwritten)) != Error::OK)
    HT_ERRORF("Problem sending response for append(%u, localfd=%u, %u) - %s",
              (unsigned)fd, (unsigned)fdata->fd, (unsigned)amount, Error::get_text(error));

}


void LocalBroker::seek(ResponseCallback *cb, uint32_t fd, uint64_t offset) {
  OpenFileDataLocalPtr fdata;
  int error;

  HT_DEBUGF("seek fd=%lu offset=%llu", (Lu)fd, (Llu)offset);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  if ((offset = (uint64_t)lseek(fdata->fd, offset, SEEK_SET)) == (uint64_t)-1) {
    report_error(cb);
    HT_ERRORF("lseek failed: fd=%d offset=%llu - %s", fdata->fd, (Llu)offset,
              strerror(errno));
    return;
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for seek(%u, %llu) - %s",
              (unsigned)fd, (Llu)offset, Error::get_text(error));

}


void LocalBroker::remove(ResponseCallback *cb, const char *fname) {
  String abspath;
  int error;

  HT_INFOF("remove file='%s'", fname);

  if (fname[0] == '/')
    abspath = m_rootdir + fname;
  else
    abspath = m_rootdir + "/" + fname;

  if (m_no_removal) {
    String deleted_file = abspath + ".deleted";
    if (!FileUtils::rename(abspath, deleted_file)) {
      report_error(cb);
      return;
    }
  }
  else {
    if (unlink(abspath.c_str()) == -1) {
      report_error(cb);
      HT_ERRORF("unlink failed: file='%s' - %s", abspath.c_str(),
                strerror(errno));
      return;
    }
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for remove(%s) - %s",
              fname, Error::get_text(error));
}


void LocalBroker::length(Response::Callback::Length *cb, const char *fname,
        bool accurate) {
  String abspath;
  uint64_t length;
  int error;

  HT_DEBUGF("length file='%s' (accurate=%s)", fname,
          accurate ? "true" : "false");

  if (fname[0] == '/')
    abspath = m_rootdir + fname;
  else
    abspath = m_rootdir + "/" + fname;

  if ((length = FileUtils::length(abspath)) == (uint64_t)-1) {
    report_error(cb);
    HT_ERRORF("length (stat) failed: file='%s' - %s", abspath.c_str(),
              strerror(errno));
    return;
  }
  
  if ((error = cb->response(length)) != Error::OK)
    HT_ERRORF("Problem sending response (%llu) for length(%s) - %s",
              (Llu)length, fname, Error::get_text(error));
}


void
LocalBroker::pread(Response::Callback::Read *cb, uint32_t fd, uint64_t offset,
                   uint32_t amount, bool) {
  OpenFileDataLocalPtr fdata;
  ssize_t nread;
  int error;

  HT_DEBUGF("pread fd=%d offset=%llu amount=%d", fd, (Llu)offset, amount);

  StaticBuffer buf((size_t)amount, (size_t)HT_DIRECT_IO_ALIGNMENT);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    m_metrics_handler->increment_error_count();
    return;
  }

  nread = FileUtils::pread(fdata->fd, buf.base, buf.aligned_size(), (off_t)offset);
  if (nread != (ssize_t)buf.aligned_size()) {
    int error = errno;
    report_error(cb);
    m_status_manager.set_read_error(error);
    HT_ERRORF("pread failed: fd=%d amount=%d aligned_size=%d offset=%llu - %s",
              fdata->fd, (int)amount, (int)buf.aligned_size(), (Llu)offset,
              strerror(errno));
    return;
  }

  m_metrics_handler->add_bytes_read(nread);
  m_status_manager.clear_status();

  if ((error = cb->response(offset, buf)) != Error::OK)
    HT_ERRORF("Problem sending response for pread(%u, %llu, %u) - %s",
              (unsigned)fd, (Llu)offset, (unsigned)amount, Error::get_text(error));
}


void LocalBroker::mkdirs(ResponseCallback *cb, const char *dname) {
  String absdir;
  int error;

  HT_DEBUGF("mkdirs dir='%s'", dname);

  if (dname[0] == '/')
    absdir = m_rootdir + dname;
  else
    absdir = m_rootdir + "/" + dname;

  if (!FileUtils::mkdirs(absdir)) {
    report_error(cb);
    HT_ERRORF("mkdirs failed: dname='%s' - %s", absdir.c_str(),
              strerror(errno));
    return;
  }

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for mkdirs(%s) - %s",
              dname, Error::get_text(error));
}


void LocalBroker::rmdir(ResponseCallback *cb, const char *dname) {
  String absdir;
  String cmd_str;
  int error;

  if (m_verbose->get())
    HT_INFOF("rmdir dir='%s'", dname);

  if (dname[0] == '/')
    absdir = m_rootdir + dname;
  else
    absdir = m_rootdir + "/" + dname;

  if (FileUtils::exists(absdir)) {
    if (m_no_removal) {
      String deleted_file = absdir + ".deleted";
      if (!FileUtils::rename(absdir, deleted_file)) {
        report_error(cb);
        return;
      }
    }
    else {
      cmd_str = (String)"/bin/rm -rf " + absdir;
      if (system(cmd_str.c_str()) != 0) {
        HT_ERRORF("%s failed.", cmd_str.c_str());
        m_metrics_handler->increment_error_count();
        cb->error(Error::FSBROKER_IO_ERROR, cmd_str);
        return;
      }
    }
  }

#if 0
  if (rmdir(absdir.c_str()) != 0) {
    report_error(cb);
    HT_ERRORF("rmdir failed: dname='%s' - %s", absdir.c_str(), strerror(errno));
    return;
  }
#endif

  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending response for mkdirs(%s) - %s",
              dname, Error::get_text(error));

}

void LocalBroker::readdir(Response::Callback::Readdir *cb, const char *dname) {
  std::vector<Filesystem::Dirent> listing;
  Filesystem::Dirent entry;
  String absdir;

  HT_DEBUGF("Readdir dir='%s'", dname);

  if (dname[0] == '/')
    absdir = m_rootdir + dname;
  else
    absdir = m_rootdir + "/" + dname;

  DIR *dirp = opendir(absdir.c_str());
  if (dirp == 0) {
    report_error(cb);
    HT_ERRORF("opendir('%s') failed - %s", absdir.c_str(), strerror(errno));
    return;
  }

  struct dirent *result;

  String full_entry_path;
  struct stat statbuf;

#if defined(USE_READDIR_R) && USE_READDIR_R
  struct dirent *dp = (struct dirent *)new uint8_t [sizeof(struct dirent)+1025];
  int ret;
#endif

  do{
    
#if defined(USE_READDIR_R) && USE_READDIR_R
    ret = readdir_r(dirp, dp, &result)
    if (ret != 0) {
      report_error(cb);
      HT_ERRORF("readdir('%s') failed - %s", absdir.c_str(), strerror(errno));
      (void)closedir(dirp);
      delete [] (uint8_t *)dp;
      return;
    }
    if(result == NULL) break;

#else
    errno = 0;
    result = ::readdir(dirp);
    if(result == NULL){
      if(errno > 0){
        report_error(cb);
        HT_ERRORF("readdir('%s') failed - %s", absdir.c_str(), strerror(errno));
        (void)closedir(dirp);
        return;
      }
      break;
    }

#endif

    if (result->d_name[0] != '.' && result->d_name[0] != 0) {
      if (m_no_removal) {
        size_t len = strlen(result->d_name);
        if (len <= 8 || strcmp(&result->d_name[len-8], ".deleted")) {
          entry.name.clear();
          entry.name.append(result->d_name);
          entry.is_dir = result->d_type == DT_DIR;
          full_entry_path.clear();
          full_entry_path.append(absdir);
          full_entry_path.append("/");
          full_entry_path.append(entry.name);
          if (stat(full_entry_path.c_str(), &statbuf) == -1) {
            if (errno != ENOENT) {
              report_error(cb);
              HT_ERRORF("readdir('%s') failed - %s", absdir.c_str(), strerror(errno));
#if defined(USE_READDIR_R) && USE_READDIR_R
              delete [] (uint8_t *)dp;
#endif
              (void)closedir(dirp);
              return;
            }
          }
          else {
            entry.length = (uint64_t)statbuf.st_size;
            entry.last_modification_time = statbuf.st_mtime;
            listing.push_back(entry);
          }
        }
      }
      else {
        entry.name.clear();
        entry.name.append(result->d_name);
        entry.is_dir = result->d_type == DT_DIR;
        full_entry_path.clear();
        full_entry_path.append(absdir);
        full_entry_path.append("/");
        full_entry_path.append(entry.name);
        if (stat(full_entry_path.c_str(), &statbuf) == -1) {
          report_error(cb);
          HT_ERRORF("readdir('%s') failed - %s", absdir.c_str(), strerror(errno));
#if defined(USE_READDIR_R) && USE_READDIR_R
          delete [] (uint8_t *)dp;
#endif
          (void)closedir(dirp);
          return;
        }
        entry.length = (uint64_t)statbuf.st_size;
        entry.last_modification_time = statbuf.st_mtime;
        listing.push_back(entry);
      }
      //HT_INFOF("readdir Adding listing '%s'", result->d_name);
    }

  }while(1);

#if defined(USE_READDIR_R) && USE_READDIR_R
  delete [] (uint8_t *)dp;
#endif

  (void)closedir(dirp);

  HT_DEBUGF("Sending back %d listings", (int)listing.size());

  cb->response(listing);
}

void LocalBroker::flush(ResponseCallback *cb, uint32_t fd) {
  this->sync(cb, fd);
}

void LocalBroker::sync(ResponseCallback *cb, uint32_t fd) {
  OpenFileDataLocalPtr fdata;

  HT_DEBUGF("sync fd=%d", fd);

  if (!m_open_file_map.get(fd, fdata)) {
    char errbuf[32];
    sprintf(errbuf, "%d", fd);
    cb->error(Error::FSBROKER_BAD_FILE_HANDLE, errbuf);
    return;
  }

  int64_t start_time = get_ts64();
  if (fsync(fdata->fd) != 0) {
    int error = errno;
    report_error(cb);
    m_status_manager.set_write_error(error);
    HT_ERRORF("sync failed: fd=%d - %s", fdata->fd, strerror(errno));
    return;
  }

  m_metrics_handler->add_sync(get_ts64() - start_time);
  m_status_manager.clear_status();

  cb->response_ok();
}


void LocalBroker::status(Response::Callback::Status *cb) {
  cb->response(m_status_manager.get());
}


void LocalBroker::shutdown(ResponseCallback *cb) {
  m_open_file_map.remove_all();
  cb->response_ok();
  this_thread::sleep_for(chrono::milliseconds(2000));
}


void LocalBroker::exists(Response::Callback::Exists *cb, const char *fname) {
  String abspath;

  HT_DEBUGF("exists file='%s'", fname);

  if (fname[0] == '/')
    abspath = m_rootdir + fname;
  else
    abspath = m_rootdir + "/" + fname;

  cb->response(FileUtils::exists(abspath));
}


void
LocalBroker::rename(ResponseCallback *cb, const char *src, const char *dst) {
  HT_INFOF("rename %s -> %s", src, dst);

  String asrc =
    format("%s%s%s", m_rootdir.c_str(), *src == '/' ? "" : "/", src);
  String adst =
    format("%s%s%s", m_rootdir.c_str(), *dst == '/' ? "" : "/", dst);

  if (std::rename(asrc.c_str(), adst.c_str()) != 0) {
    report_error(cb);
    return;
  }
  cb->response_ok();
}

void
LocalBroker::debug(ResponseCallback *cb, int32_t command,
                   StaticBuffer &serialized_parameters) {
  HT_ERRORF("debug command %d not implemented.", command);
  cb->error(Error::NOT_IMPLEMENTED, format("Unsupported debug command - %d",
            command));
}



void LocalBroker::report_error(ResponseCallback *cb) {
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
