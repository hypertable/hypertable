/* -*- c++ -*-
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

#include "Common/Compat.h"
#include <cassert>
#include <cstdlib>

#include "AsyncComm/Comm.h"

#include "Common/Init.h"
#include "Common/Logger.h"
#include "Common/System.h"
#include "Common/String.h"
#include "Common/Usage.h"

#include "Hypertable/Lib/Config.h"
#include "Hypertable/Lib/CommitLog.h"
#include "Hypertable/Lib/CommitLogReader.h"

#include "FsBroker/Lib/Client.h"

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {
  struct MyPolicy : Policy {
    static void init_options() {
      cmdline_desc().add_options()
        ("roll-limit", i64(2000), "Commit log roll limit in bytes")
        ;
      alias("roll-limit", "Hypertable.RangeServer.CommitLog.RollLimit");
    }
  };

  typedef Meta::list<MyPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

  //void test1(FsBroker::Lib::Client *fs_client);
  void test_link(FsBroker::Lib::ClientPtr &client);
  void write_entries(CommitLog *log, int num_entries, uint64_t *sump,
                     CommitLogBase *link_log);
  void read_entries(CommitLogReader *log_reader, uint64_t *sump);
}


int main(int argc, char **argv) {
  try {
    init_with_policies<Policies>(argc, argv);

    Comm *comm = Comm::instance();
    ConnectionManagerPtr conn_mgr = make_shared<ConnectionManager>(comm);
    int timeout = has("fs-timeout") ? get_i32("fs-timeout") : 180000;

    /**
     * connect to FS broker
     */
    InetAddr addr(get_str("fs-host"), get_i16("fs-port"));
    FsBroker::Lib::ClientPtr fs = std::make_shared<FsBroker::Lib::Client>(conn_mgr, addr, timeout);

    if (!fs->wait_for_connection(10000)) {
      HT_ERROR("Unable to connect to FS Broker, exiting...");
      exit(EXIT_FAILURE);
    }

    srandom(1);

    //test1(fs);
    test_link(fs);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }

  return 0;
}


namespace {

#if 0
  void test1(FsBroker::Lib::Client *fs_client) {
    String log_dir = "/hypertable/test_log";
    String fname;
    CommitLog *log;
    CommitLogReader *log_reader;
    uint64_t sum_written = 0;
    uint64_t sum_read = 0;

    // Remove /hypertable/test_log
    fs_client->rmdir(log_dir);

    fname = log_dir + "/c";

    // Create /hypertable/test_log/c
    fs_client->mkdirs(fname);

    FilesystemPtr fs = fs_client;

    log = new CommitLog(fs, fname, properties);

    write_entries(log, 20, &sum_written, 0);

    log->close();

    delete log;

    log_reader = new CommitLogReader(fs, fname);

    read_entries(log_reader, &sum_read);

    delete log_reader;

    HT_ASSERT(sum_read == sum_written);
  }
#endif

  void test_link(FsBroker::Lib::ClientPtr &client) {
    String log_dir = "/hypertable/test_log";
    String fname;
    CommitLog *log;
    CommitLogReaderPtr log_reader_ptr;
    uint64_t sum_written = 0;
    uint64_t sum_read = 0;
    FilesystemPtr fs = client;

    // Remove /hypertable/test_log
    client->rmdir(log_dir);

    // Create log directories
    client->mkdirs(log_dir + "/a");
    client->mkdirs(log_dir + "/b");
    client->mkdirs(log_dir + "/c");
    client->mkdirs(log_dir + "/d");

    /**
     * Create log "c"
     */
    fname = log_dir + "/c";
    log = new CommitLog(fs, fname, properties);
    write_entries(log, 20, &sum_written, 0);
    delete log;

    log_reader_ptr = make_shared<CommitLogReader>(fs, fname);
    read_entries(log_reader_ptr.get(), &sum_read);

    /**
     * Create log "b" and link in log "c"
     */
    fname = log_dir + "/b";
    log = new CommitLog(fs, fname, properties);
    write_entries(log, 20, &sum_written, log_reader_ptr.get());
    delete log;

    /**
     * Create log "d"
     */
    fname = log_dir + "/d";
    log = new CommitLog(fs, fname, properties);
    write_entries(log, 20, &sum_written, 0);
    delete log;

    /**
     * Create log "a" and link in "b" and "d"
     */
    fname = log_dir + "/a";
    log = new CommitLog(fs, fname, properties);

    // Open "b", read it, and link it into "a"
    fname = log_dir + "/b";
    log_reader_ptr = make_shared<CommitLogReader>(fs, fname);
    read_entries(log_reader_ptr.get(), &sum_read);
    write_entries(log, 20, &sum_written, log_reader_ptr.get());

    // Open "d", read it, and link it into "a"
    fname = log_dir + "/d";
    log_reader_ptr = make_shared<CommitLogReader>(fs, fname);
    read_entries(log_reader_ptr.get(), &sum_read);
    write_entries(log, 20, &sum_written, log_reader_ptr.get());

    delete log;

    sum_read = 0;
    fname = log_dir + "/a";
    log_reader_ptr = make_shared<CommitLogReader>(fs, fname);
    read_entries(log_reader_ptr.get(), &sum_read);

    HT_ASSERT(sum_read == sum_written);
  }

  void
  write_entries(CommitLog *log, int num_entries, uint64_t *sump,
                CommitLogBase *link_log) {
    int error;
    int64_t revision;
    uint32_t limit;
    uint32_t payload[101];
    size_t link_point = 50;
    DynamicBuffer dbuf;

    if (link_log)
      link_point = random() % 50;

    for (size_t i=0; i<50; i++) {
      revision = log->get_timestamp();

      if (i == link_point) {
        if ((error = log->link_log(0, link_log)) != Error::OK)
          HT_THROW(error, "Problem writing to log file");
      }
      else {
        limit = (random() % 100) + 1;
        for (size_t j=0; j<limit; j++) {
          payload[j] = random();
          *sump += payload[j];
        }

        dbuf.base = (uint8_t *)payload;
        dbuf.ptr = dbuf.base + (4*limit);
        dbuf.own = false;

        if ((error = log->write(0, dbuf, revision, Filesystem::Flags::FLUSH)) != Error::OK)
          HT_THROW(error, "Problem writing to log file");
      }
    }
  }

  void read_entries(CommitLogReader *log_reader, uint64_t *sump) {
    const uint8_t *block;
    size_t block_len;
    uint32_t *iptr;
    size_t icount;
    BlockHeaderCommitLog header;

    while (log_reader->next(&block, &block_len, &header)) {
      assert((block_len % 4) == 0);
      icount = block_len / 4;
      iptr = (uint32_t *)block;
      for (size_t i=0; i<icount; i++)
        *sump += iptr[i];
    }
  }
}
