/*
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

#include <Common/Compat.h>

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/old/RangeServerMetaLog.h>
#include <Hypertable/Lib/old/RangeServerMetaLogReader.h>
#include <Hypertable/Lib/old/MasterMetaLogReader.h>

#include <FsBroker/Lib/Client.h>

#include <Common/Init.h>
#include <Common/Logger.h>

using namespace Hypertable;
using namespace Hypertable::OldMetaLog;
using namespace Config;

namespace {

struct MyPolicy : Policy {
  static void init_options() {
    cmdline_desc("Usage: %s [options] <path>\n\nOptions").add_options()
      ("states,s", "Dump metalog as states tree")
      ("copy", str(), "Make a copy of the metalog to <arg> until errors")
      ;
    cmdline_hidden_desc().add_options()
    ("path", str(), "path in FS")
    ("path", -1);
  }
  static void init() {
    if (!has("path")) {
      HT_ERROR_OUT <<"path required\n"<< cmdline_desc() << HT_END;
      std::exit(EXIT_FAILURE);
    }
  }
};

typedef Meta::list<MyPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

void dump_range_states(RangeServerMetaLogReader *rdr) {
  bool found_recover_entry;
  const RangeStates &rstates = rdr->load_range_states(&found_recover_entry);

  if (found_recover_entry)
    std::cout << "Found recover entry" << std::endl;
  else
    std::cout << "Recover entry not found" << std::endl;
  for (const auto i : rstates) std::cout << *i;
}

void dump_metalog(Filesystem &fs, const String &path, PropertiesPtr &cfg) {
  try {
    // TODO: auto sensing master metalog
    RangeServerMetaLogReaderPtr rdr = new RangeServerMetaLogReader(&fs, path);

    if (cfg->has("states")) {
      dump_range_states(rdr.get());
      return;
    }

    MetaLogEntryPtr entry;

    if (cfg->has("copy")) {
      RangeServerMetaLogPtr log =
          new RangeServerMetaLog(&fs, cfg->get_str("copy"));

      while ((entry = rdr->read()))
        log->write(entry.get());
    }
    else {
      while ((entry = rdr->read()))
        std::cout << entry.get() <<"\n";
    }
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }
}

} // local namespace

int main(int ac, char *av[]) {
  try {
    init_with_policies<Policies>(ac, av);

    FsBroker::Lib::Client *fs = new FsBroker::Lib::Client(get_str("fs-host"),
        get_i16("fs-port"), get_i32("timeout"));

    dump_metalog(*fs, get_str("path"), properties);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  return 0;
}
