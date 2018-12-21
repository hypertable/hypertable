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

#include <FsBroker/Lib/Client.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/Config.h>

#include <Common/StringExt.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>
#include <Common/System.h>
#include <Common/Init.h>
#include <Common/Usage.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>

extern "C" {
#include <sys/types.h>
#include <unistd.h>
}

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {
  const char *required_files[] = {
    "./hypertable_ldi_select_test",
    "./hypertable.cfg",
    "./hypertable_ldi_stdin_test_load.hql",
    "./hypertable_ldi_stdin_test_select.hql",
    "./hypertable_test.tsv.gz",
    "./hypertable_ldi_select_test.golden",
    0
  };

  static const size_t amount = 4096;

  bool copyToDfs(FsBroker::Lib::Client *client, const char *src, const char *dst) {
    client->mkdirs("/ldi_test");

    std::filebuf src_file;
    if(!src_file.open(src, std::ios_base::in))
      return false;
    
    int fd = client->create(dst, Filesystem::OPEN_FLAG_OVERWRITE, -1, -1, -1);
    if(fd<0)
      return false;

    StaticBuffer buf(amount, HT_DIRECT_IO_ALIGNMENT);
    while((buf.size = src_file.sgetn(reinterpret_cast<char*>(buf.base), amount))) {
      client->append(fd, buf);
    }
    client->close(fd);

    return true;
  }

  bool copyFromDfs(FsBroker::Lib::Client *client, const char *src, const char *dst) {
    client->mkdirs("/ldi_test");

    int fd=client->open(src,0);
    if(fd<0)
      return false;

    std::ofstream dst_file(dst);
    if(!dst_file.is_open())
      return false;

    int nread;
    StaticBuffer buf(amount, HT_DIRECT_IO_ALIGNMENT);
    while((nread = client->read(fd, buf.base, amount)) > 0) {
      dst_file << std::string(reinterpret_cast<const char*>(buf.base), nread);
    }
    client->close(fd);

    return true;
  }

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc().add_options()
        ("install-dir", str("/opt/hypertable/current"), 
         "Path to hypertable installation")
      ;
    }
  };
    
  typedef Meta::list<AppPolicy, DefaultCommPolicy> Policies;

}


int main(int argc, char **argv) {
  init_with_policies<Policies>(argc, argv);
  String cmd_str;
  
  String install_dir = get_str("install-dir");
  System::initialize(argv[0]);

  for (int i=0; required_files[i]; i++) {
    if (!FileUtils::exists(required_files[i])) {
      HT_ERRORF("Unable to find '%s'", required_files[i]);
      quick_exit(EXIT_FAILURE);
    }
  }

  String host = get_str("FsBroker.Host");
  uint16_t port = get_i16("FsBroker.Port");
  uint32_t timeout_ms = get_i32("Hypertable.Request.Timeout");

  InetAddr addr;
  InetAddr::initialize(&addr, host.c_str(), port);
  DispatchHandlerSynchronizer *sync_handler = new DispatchHandlerSynchronizer();
  DispatchHandlerPtr default_handler(sync_handler);
  EventPtr event;
  Comm *comm = Comm::instance();

  comm->connect(addr, default_handler);
  sync_handler->wait_for_reply(event);
  if(event->type == Event::DISCONNECT || event->error == Error::COMM_CONNECT_ERROR) {
    HT_ERRORF("Unable to connect to %s:%d", host.c_str(), port);
    exit(EXIT_FAILURE);
  }


  FsBroker::Lib::Client *client = new FsBroker::Lib::Client(comm, addr, timeout_ms);

  /**
   * LDI and Select using stdin
   */
  cmd_str = "./ht_hypertable --test-mode --config hypertable.cfg "
      "< hypertable_ldi_stdin_test_load.hql > hypertable_ldi_select_test.output 2>&1";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  cmd_str = "./ht_hypertable --test-mode --config hypertable.cfg "
      "< hypertable_ldi_stdin_test_select.hql >> hypertable_ldi_select_test.output 2>&1";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  /**
   * LDI and Select using FsBroker
   */


  clog << "Completed Select using stdin\n";

  copyToDfs(client, "hypertable_test.tsv.gz", "/ldi_test/hypertable_test.tsv.gz");

  String hql = (String)" USE \"/test\";" +
                " DROP TABLE IF EXISTS hypertable;" +
                " CREATE TABLE hypertable ( TestColumnFamily );" +
                " LOAD DATA INFILE ROW_KEY_COLUMN=rowkey" +
                " \"fs:///ldi_test/hypertable_test.tsv.gz\"" +
                " INTO TABLE hypertable;"
                ;
  // load from dfs zipped file
  cmd_str = "./ht_hypertable --test-mode --config hypertable.cfg --exec '"+ hql + "'";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);
  // select into dfs zipped file
  hql = "USE \"/test\"; SELECT * FROM hypertable INTO FILE \"fs:///ldi_test/dfs_select.gz\";";
  cmd_str = "./ht_hypertable --test-mode --config hypertable.cfg --exec '"+ hql + "'";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

#if 0
  // cp from dfs dir
  cmd_str = "cp " + test_dir + "dfs_select.gz .";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  cmd_str = "rm -rf " + test_dir;
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);
#endif

  copyFromDfs(client, "/ldi_test/dfs_select.gz", "dfs_select.gz");

  cmd_str = "gunzip -f dfs_select.gz";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  cmd_str = "cat dfs_select >> hypertable_ldi_select_test.output ";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  cmd_str = "diff hypertable_ldi_select_test.output hypertable_ldi_select_test.golden";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  hql = (String)" USE \"/test\";" +
    " DROP TABLE IF EXISTS hypertable;" +
    " CREATE TABLE hypertable ( TestColumnFamily );" +
    " LOAD DATA INFILE \"hypertable_escape_test.tsv\" INTO TABLE hypertable;" +
    " SELECT * FROM hypertable NO_ESCAPE INTO FILE \"hypertable_escape_test.output\";"
    ;

  // load from dfs zipped file
  cmd_str = "./ht_hypertable --test-mode --config hypertable.cfg --exec '"+ hql + "'";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  cmd_str = "diff hypertable_escape_test.output hypertable_escape_test.golden";
  if (system(cmd_str.c_str()) != 0)
    quick_exit(EXIT_FAILURE);

  quick_exit(EXIT_SUCCESS);
}
