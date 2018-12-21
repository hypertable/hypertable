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

#include "Common/Compat.h"
#include "Common/Error.h"
#include "Common/InetAddr.h"
#include "Common/Logger.h"
#include "Common/Init.h"

#include "AsyncComm/Comm.h"
#include "AsyncComm/ConnectionManager.h"

#include "FsBroker/Lib/Config.h"
#include "FsBroker/Lib/Client.h"

#include "Hypertable/Lib/CommitLog.h"
#include "Hypertable/Lib/CommitLogReader.h"

#include <cstdlib>

#include <boost/algorithm/string/predicate.hpp>

#include "EmitterStdout.h"
#include "EmitterTsv.h"

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  const char *usage = R"(
Usage: ht log_player [options] <log-path>

This program converts cells in a commit log (in the brokered
file system) into a form that allows them to be re-inserted
into Hypertable.  It can be used in situations where
Hypertable is not coming up due to major filesystem
corruption and you need to recover data that has be inserted
into the database.  There are four different commit logs for
each range server, three logs that persist writes to
internal system tables (root, metadata, system) and one
commit log for application tables (user).  These logs can be
found, for each range server, under the following paths:

/hypertable/servers/rs*/log/root
/hypertable/servers/rs*/log/metadata
/hypertable/servers/rs*/log/system
/hypertable/servers/rs*/log/user

For this tool to work properly, it needs access to
both Hyperspace and the FS broker.

The tool can be run in two modes.  The first mode, specified
with the --tsv-output switch, causes the cells in the log to
be written into a tree of .tsv files rooted in the current
working directory.  The directory tree produced parallels
the namespace hierarchy of the tables encountered in the
log, with a .tsv file for each unique table encountered in
the log.  For example:

$ ht log_player --tsv-output /hypertable/servers/rs6/log/user

$ tree
.
├── alerts
│   └── realtime.tsv
├── cache
│   └── image.tsv
└── search
    ├── blog.tsv
    ├── image.tsv
    └── news.tsv

The .tsv files generated in the --tsv-output mode can be
loaded into re-created tables using the LOAD DATA INFILE
command.

The second mode, specified with the --stdout switch, causes
the cells to be written to the terminal in the following
format:

<tablename> '\t' <timestamp> '\t' <rowkey> '\t' <column> '\t' <value>

For example:

$ ht log_player --stdout /hypertable/servers/rs6/log/user

LoadTest  1456961038717377419  212321801        source  ively\\nundiffractiveness\\nundi
LoadTest  1456961038717377420  212322677        source  s\\nrestrip\\nrestrive\\nrestrive
LoadTest  1456961038717377421  212323185        source  sly\\nsacrilegiousness\\nsacrile
LoadTest  1456961038717377422  212324729        source  s\\npsammophile\\npsammophilous\\
LoadTest  1456961038717377423  212325006        source  cartilaginous\\ncarting\\ncartis
LoadTest  1456961038717377424  212325929        source  nah\\nwinnard\\nWinne\\nWinnebago
...

Options)";

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("tsv-output", "Convert log into a set of loadable .tsv files")
        ("stdout", "Write log to stdout with table name as first field")
        ;
      cmdline_hidden_desc().add_options()
      ("log-dir", str(), "dfs log dir")
      ("log-dir", -1);
    }
    static void init() {
      if (!has("log-dir")) {
        HT_ERROR_OUT <<"log-dir required\n"<< cmdline_desc() << HT_END;
        exit(1);
      }
    }
  };

  typedef Meta::list<AppPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

  void
  display_log(FsBroker::Lib::ClientPtr &dfs_client, CommitLogReader *log_reader,
              Emitter *emitter) {
    BlockHeaderCommitLog header;
    const uint8_t *base;
    size_t len;
    const uint8_t *ptr, *end;
    TableIdentifier table;
    ByteString bs;
    Key key;
    const uint8_t *value;
    size_t value_len;

    while (log_reader->next(&base, &len, &header)) {

      HT_ASSERT(header.check_magic(CommitLog::MAGIC_DATA));

      ptr = base;
      end = base + len;

      table.decode(&ptr, &len);

      while (ptr < end) {

        // extract the key
        bs.ptr = ptr;
        key.load(bs);

        bs.next();

        value_len = bs.decode_length(&value);

        emitter->add(table, key, value, value_len);

        // skip value
        bs.next();
        ptr = bs.ptr;

        if (ptr > end)
          HT_THROW(Error::REQUEST_TRUNCATED, "Problem decoding value");
      }
    }
  }
} // local namespace


int main(int argc, char **argv) {
  Emitter *emitter = 0;

  try {
    init_with_policies<Policies>(argc, argv);

    ConnectionManagerPtr conn_manager_ptr = make_shared<ConnectionManager>();

    String log_dir = get_str("log-dir");
    String log_host = get("log-host", String());
    int timeout = get_i32("dfs-timeout", 15000);
    bool stdout = has("stdout");
    bool tsv_output = has("tsv-output");

    if (stdout && tsv_output)
      HT_FATAL("Only one of --stdout and --tsv-output can be supplied.");

    /**
     * Check for and connect to commit log DFS broker
     */
    FsBroker::Lib::ClientPtr dfs_client;

    if (log_host.length()) {
      int log_port = get_i16("log-port");
      InetAddr addr(log_host, log_port);

      dfs_client = make_shared<FsBroker::Lib::Client>(conn_manager_ptr, addr, timeout);
    }
    else {
      dfs_client = make_shared<FsBroker::Lib::Client>(conn_manager_ptr, properties);
    }

    if (!dfs_client->wait_for_connection(timeout)) {
      HT_ERROR("Unable to connect to DFS Broker, exiting...");
      exit(1);
    }

    FilesystemPtr fs = static_pointer_cast<Filesystem>(dfs_client);

    boost::trim_right_if(log_dir, boost::is_any_of("/"));    

    CommitLogReaderPtr log_reader = make_shared<CommitLogReader>(fs, log_dir);

    if (stdout)
      emitter = new EmitterStdout();
    else if (tsv_output)
      emitter = new EmitterTsv();

    display_log(dfs_client, log_reader.get(), emitter);

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    delete emitter;
    return 1;
  }
  delete emitter;
  return 0;
}

