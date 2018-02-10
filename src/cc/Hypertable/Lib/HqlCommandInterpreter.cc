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

#include "Client.h"
#include "HqlCommandInterpreter.h"
#include "HqlHelpText.h"
#include "HqlParser.h"
#include "Key.h"
#include "LoadDataSource.h"
#include "Schema.h"

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Stopwatch.h>

#include <boost/progress.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace std;
using namespace Hypertable;
using namespace Hql;

namespace {

  struct CommandCallback : HqlInterpreter::Callback {
    CommandInterpreter &commander;
    int command {};
    unique_ptr<boost::progress_display> progress;
    Stopwatch stopwatch;
    bool m_profile {};

    CommandCallback(CommandInterpreter &interp, bool profile=false)
      : HqlInterpreter::Callback(interp.normal_mode()), commander(interp),
        m_profile(profile) {
      format_ts_in_nanos = interp.timestamp_output_format()
          == CommandInterpreter::TIMESTAMP_FORMAT_NANOS;
      output = stdout; // set to stdout
    }

    ~CommandCallback() {
      if (output == stdout || output == stderr)
        fflush(output);
      else if (output)
        fclose(output);
    }

    void on_parsed(ParserState &state) override { command = state.command; }

    void on_return(const string &str) override { cout << str << endl; }

    void on_update(size_t total) override {
      if (!normal_mode)
        return;

      HT_ASSERT(command);

      if (command == COMMAND_LOAD_DATA) {
        HT_ASSERT(!progress);
        cout <<"\nLoading "<< format_number(total)
             <<" bytes of input data..." << endl;
        progress = std::make_unique<boost::progress_display>(total);
      }
    }

    void on_progress(size_t amount) override {
      HT_ASSERT(progress);
      *progress += amount;
    }

    void on_finish(TableMutatorPtr &mutator) override {
      Callback::on_finish(mutator);
      stopwatch.stop();

      if (normal_mode) {
        if (progress && progress->count() < file_size)
          *progress += file_size - progress->count();

        double elapsed = stopwatch.elapsed();

        if (command == COMMAND_LOAD_DATA)
          fprintf(stderr, "Load complete.\n");

        fputc('\n', stderr);
        fprintf(stderr, "  Elapsed time:  %.2f s\n", elapsed);

        if (total_values_size)
          fprintf(stderr, "Avg value size:  %.2f bytes\n",
                 (double)total_values_size / total_cells);

        if (total_keys_size)
          fprintf(stderr, "  Avg key size:  %.2f bytes\n",
                 (double)total_keys_size / total_cells);

        if (total_keys_size && total_values_size) {
          fprintf(stderr, "    Throughput:  %.2f bytes/s",
                 (total_keys_size + total_values_size) / elapsed);

          if (file_size)
            fprintf(stderr, " (%.2f bytes/s)", file_size / elapsed);

          fputc('\n', stderr);
        }
        if (total_cells) {
          fprintf(stderr, "   Total cells:  %llu\n", (Llu)total_cells);
          fprintf(stderr, "    Throughput:  %.2f cells/s\n",
                 total_cells / elapsed);
        }
        if (mutator)
          fprintf(stderr, "       Resends:  %llu\n",
                  (Llu)mutator->get_resend_count());

        fflush(stderr);
      }
    }

    void on_finish(TableScannerPtr &scanner) override {
      if (scanner && m_profile) {
        fputc('\n', stderr);
        fprintf(stderr, "  Elapsed time:  %lld ms\n", (Lld)stopwatch.elapsed_millis());
        ProfileDataScanner profile_data;
        scanner->get_profile_data(profile_data);
        fprintf(stderr, " Cells scanned:  %lld\n", (Lld)profile_data.cells_scanned);
        fprintf(stderr, "Cells returned:  %lld\n", (Lld)profile_data.cells_returned);
        fprintf(stderr, " Bytes scanned:  %lld\n", (Lld)profile_data.bytes_scanned);
        fprintf(stderr, "Bytes returned:  %lld\n", (Lld)profile_data.bytes_returned);
        fprintf(stderr, "     Disk read:  %lld\n", (Lld)profile_data.disk_read);
        fprintf(stderr, "   Scan blocks:  %d\n", (int)profile_data.scanblocks);
        fprintf(stderr, "  Sub scanners:  %d\n", (int)profile_data.subscanners);
        string servers;
        bool first = true;
        for (auto & server : profile_data.servers) {
          if (first)
            first = false;
          else
            servers += ",";
          servers += server;
        }
        fprintf(stderr, "       Servers:  %s\n", servers.c_str());
        fputc('\n', stderr);
        fflush(stderr);
      }
    }

  };

} // local namespace


HqlCommandInterpreter::HqlCommandInterpreter(Client *client, bool profile)
  : m_interp(client->create_hql_interpreter(false)), m_profile(profile) {
}


HqlCommandInterpreter::HqlCommandInterpreter(HqlInterpreter *interp)
    : m_interp(interp) {
}


int HqlCommandInterpreter::execute_line(const string &line) {
  CommandCallback cb(*this, m_profile);
  return m_interp->execute(line, cb);
}

