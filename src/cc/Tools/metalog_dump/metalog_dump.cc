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

#include <Hypertable/Master/MetaLogDefinitionMaster.h>

#include <Hypertable/RangeServer/MetaLogDefinitionRangeServer.h>
#include <Hypertable/RangeServer/MetaLogEntityRange.h>

#include <Hypertable/Lib/MetaLog.h>
#include <Hypertable/Lib/MetaLogReader.h>

#include <FsBroker/Lib/Client.h>
#include <FsBroker/Lib/Config.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/InetAddr.h>
#include <Common/Init.h>
#include <Common/Logger.h>
#include <Common/Usage.h>

#include <boost/algorithm/string.hpp>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc("Usage: %s [options] <log-dir>\n\n"
                   "  This program dumps a textual representation of\n"
                   "  the given metalog.  If the last path component\n"
                   "  is a number, then the it is assumed to be an\n"
                   "  individual log file, otherwise the path is assumed\n"
                   "  to be the metalog directory\n\nOptions")
        .add_options()
        ("all", "Display all entities in log (not just latest state)")
        ("metadata-tsv", "For each Range, dump StartRow and Location .tsv lines")
        ("location", str(""), "Used with --metadata-tsv to specify location proxy")
        ("print-logs", boo(false)->zero_token(), "Print log files, one per line")
        ("show-version", "Display log version number and exit")
        ;
      cmdline_hidden_desc().add_options()
      ("log-path", str(), "dfs log path")
      ("log-path", -1);
    }
    static void init() {
      if (!has("log-path")) {
        HT_ERROR_OUT <<"log-path required\n"<< cmdline_desc() << HT_END;
        exit(EXIT_FAILURE);
      }
    }
  };

  typedef Meta::list<FsClientPolicy, DefaultCommPolicy, AppPolicy> Policies;

  void determine_log_type(FilesystemPtr &fs, String path, String &name, bool *is_file) {
    bool check_file=false;
    boost::trim_right_if(path, boost::is_any_of("/"));

    *is_file = false;

    const char *base, *ptr;
    if ((base = strrchr(path.c_str(), '/')) != (const char *)0) {
      for (ptr=base+1; *ptr && isdigit(*ptr); ptr++)
        ;
      if (*ptr == 0)
        *is_file = true;
    }

    if (!*is_file) {
      std::vector<Filesystem::Dirent> listing;
      fs->readdir(path, listing);
      if (!listing.empty()) {
        path += String("/") + listing[0].name;
        check_file = true;
      }
    }

    if (*is_file || check_file) {
      int fd = fs->open(path, Filesystem::OPEN_FLAG_VERIFY_CHECKSUM);
      MetaLog::Header header;
      uint8_t buf[MetaLog::Header::LENGTH];
      const uint8_t *ptr = buf;
      size_t remaining = MetaLog::Header::LENGTH;

      size_t nread = fs->read(fd, buf, MetaLog::Header::LENGTH);

      if (nread != MetaLog::Header::LENGTH)
        HT_THROWF(Error::METALOG_BAD_HEADER,
                  "Short read of header for '%s' (expected %d, got %d)",
                  path.c_str(), (int)MetaLog::Header::LENGTH, (int)nread);

      header.decode(&ptr, &remaining);
      name = header.name;
      fs->close(fd);
    }
    else
      name = String(base+1);
  }


} // local namespace


int main(int argc, char **argv) {
  try {
    init_with_policies<Policies>(argc, argv);

    ConnectionManagerPtr conn_manager_ptr = make_shared<ConnectionManager>();

    String log_path = get_str("log-path");
    String log_host = get("log-host", String());
    int timeout = has("dfs-timeout") ? get_i32("dfs-timeout") : 10000;
    bool dump_all = has("all");
    bool show_version = has("show-version");
    bool metadata_tsv = has("metadata-tsv");
    bool print_logs = get_bool("print-logs");
    String location;

    if (metadata_tsv) {
      if (has("location") && get_str("location").size()>0)
        location = get_str("location");
      else
        location = FileUtils::file_to_string(System::install_dir + (String)"/run/location");
    }

    /**
     * Check for and connect to commit log DFS broker
     */
    FsBroker::Lib::ClientPtr dfs_client;

    if (log_host.length()) {
      int log_port = get_i16("log-port");
      InetAddr addr(log_host, log_port);

      dfs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager_ptr, addr, timeout);
    }
    else {
      dfs_client = std::make_shared<FsBroker::Lib::Client>(conn_manager_ptr, properties);
    }

    if (!dfs_client->wait_for_connection(timeout)) {
      HT_ERROR("Unable to connect to DFS Broker, exiting...");
      exit(EXIT_FAILURE);
    }

    // Population Defintion map
    std::unordered_map<String, MetaLog::DefinitionPtr> defmap;
    MetaLog::DefinitionPtr def = make_shared<MetaLog::DefinitionRangeServer>("");
    defmap[def->name()] = def;
    def = make_shared<MetaLog::DefinitionMaster>("");
    defmap[def->name()] = def;

    FilesystemPtr fs = dfs_client;
    MetaLog::ReaderPtr rsml_reader;
    String name;
    bool is_file;

    determine_log_type(fs, log_path, name, &is_file);

    auto iter = defmap.find(name);
    if (iter == defmap.end()) {
      cerr << "No definition for log type '" << name << "'" << endl;
      exit(EXIT_FAILURE);
    }
    def = iter->second;

    int reader_flags = dump_all ? MetaLog::Reader::LOAD_ALL_ENTITIES : 0;
    if (is_file) {
      rsml_reader = make_shared<MetaLog::Reader>(fs, def, reader_flags);
      rsml_reader->load_file(log_path);
    }
    else
      rsml_reader = make_shared<MetaLog::Reader>(fs, def, log_path, reader_flags);

    if (!metadata_tsv && !print_logs)
      cout << "log version: " << rsml_reader->version() << "\n";

    if (show_version)
      quick_exit(EXIT_SUCCESS);

    std::vector<MetaLog::EntityPtr> entities;

    if (dump_all)
      rsml_reader->get_all_entities(entities);
    else
      rsml_reader->get_entities(entities);

    MetaLogEntityRange *entity_range;
    if (metadata_tsv) {
      for (size_t i=0; i<entities.size(); i++) {
        entity_range = dynamic_cast<MetaLogEntityRange *>(entities[i].get());
        if (entity_range) {
          TableIdentifier table;
          String start_row, end_row;
          entity_range->get_table_identifier(table);
          entity_range->get_boundary_rows(start_row, end_row);
          cout << table.id << ":" << end_row << "\tStartRow\t" << start_row << "\n";
          cout << table.id << ":" << end_row << "\tLocation\t" << location << "\n";
        }
      }
    }
    else if (print_logs) {
      for (auto &entity : entities) {
        entity_range = dynamic_cast<MetaLogEntityRange *>(entity.get());
        if (entity_range) {
          String log = entity_range->get_transfer_log();
          if (!log.empty())
            std::cout << log << "\n";
        }
      }
      std::cout << std::flush;
    }
    else {
      for (size_t i=0; i<entities.size(); i++)
        cout << *entities[i] << "\n";
    }

    cout << flush;

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  quick_exit(EXIT_SUCCESS);
}
