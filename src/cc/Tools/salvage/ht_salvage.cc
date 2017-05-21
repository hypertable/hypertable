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

#include <FsBroker/Lib/Client.h>
#include <FsBroker/Lib/Config.h>

#include <Hyperspace/Session.h>

#include <Hypertable/RangeServer/CellStoreFactory.h>
#include <Hypertable/RangeServer/CellListScanner.h>
#include <Hypertable/RangeServer/Global.h>
#include <Hypertable/RangeServer/MergeScannerRange.h>

#include <Hypertable/Lib/HqlCommandInterpreter.h>
#include <Hypertable/Lib/LoadDataEscape.h>
#include <Hypertable/Lib/Schema.h>

#include <AsyncComm/Comm.h>

#include <Common/Config.h>
#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Init.h>
#include <Common/InteractiveCommand.h>
#include <Common/Properties.h>
#include <Common/Serialization.h>
#include <Common/Stopwatch.h>
#include <Common/StringExt.h>
#include <Common/System.h>
#include <Common/Timer.h>
#include <Common/Usage.h>

#include <re2/re2.h>

#include <boost/algorithm/string.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/device/null.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/progress.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/tokenizer.hpp>

#include <cstring>
#include <iostream>
#include <fstream>
#include <set>
#include <vector>

extern "C" {
#include <time.h>
}

using namespace Hypertable;
using namespace Hyperspace;
using namespace Config;
using namespace std;
using namespace boost;

namespace {

  const char *usage = R"(
Usage: ht salvage [options] <output-directory>

This tool will salvage data from a (potentially corrupt)
Hypertable database by recursively walking the
/hypertable/tables directory in the brokered filesystem
looking for CellStore files and extracting data from them.
The recovered data is written into a tree of .tsv files that
can be re-loaded into a clean database at a later time.  For
each unique table directory encountered, a .hql file will
also be generated which contains the HQL required to
re-create the table.

This tool requires Hyperspace to be intact and running and
an FS broker running on local host.  These services can be
started as follows:

$ ht cluster start_hyperspace
$ ht start-fsbroker hadoop

This tool only extracts data from CellStores.  To extract
data in commit logs, the log_player tool can be used.


BASIC USAGE

  The simplest usage is to run it with no options, supplying
  only the output directory argument:

  $ ht salvage output

  After successfully completing, the output directory will
  be populated with the namespace hierarchy and the .tsv and
  .hql files.  For example:

  $ tree output
  output
  ├── alerts
  │   ├── create-realtime.hql
  │   └── realtime.tsv
  ├── cache
  │   ├── create-image.hql
  │   └── image.tsv
  └── search
      ├── blog.tsv
      ├── create-blog.hql
      ├── create-image.hql
      ├── create-news.hql
      ├── image.tsv
      └── news.tsv


INCLUDE AND EXCLUDE

  To exclude a specific namespace or table, the --exclude
  option may be used:

  $ ht salvage --exclude search output

  $ tree output
  output
  ├── alerts
  │   ├── create-realtime.hql
  │   └── realtime.tsv
  └── cache
      ├── create-image.hql
      └── image.tsv

  To include a specific namespace or table, the --include
  option may be used:

  $ ht salvage --include search/blog output

  $ tree output
  output
  └── search
      ├── blog.tsv
      └── create-blog.hql


RESTRICTING ROW SPACE

  To restrict the salvaged data to a specific row key range,
  use the --start-key and --end-key options.  For example:  

  $ ht salvage --start-key "019999999" --end-key "030000000" output


PATH REGEX

  With the --path-regex option, a regular expression can be
  supplied to specify which directories in the brokered
  filesystem should be included in the recovery.  For
  example:

  $ ht salvage --verbose --path-regex "/hypertable/tables/[2-3]" output

Options)";

  struct MyPolicy : Config::Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("dry-run", "Dont actually open the CellStores, just print out messages")
        ("exclude", str(), "Comma-separated namespaces and/or tables to exclude")
        ("include", str(), "Comma-separated namespaces and/or tables to include")
        ("end-key", str(), "Ignore keys that are greater than <arg>")
        ("start-key", str(), "Ignore keys that are less than or equal to <arg>")
        ("path-regex", str(), "Just salvage CellStores files whose full path matches this regex")
	("cumulative-size-estimate", i64(),
	 "Estimate of cumulative size of files.  Used for for progress tracking.")
        ("exclude-metadata-since", i32(),
         "Exclude files referenced in this METADATA that are older than this many seconds ago")
	("gzip", "Generate gzipped .tsv files")
        ;
      cmdline_hidden_desc().add_options()("out-dir", str(), "output dir");
      cmdline_positional_desc().add("out-dir", -1);
    }
    static void init() {
      if (!has("out-dir")) {
        HT_ERROR_OUT <<"output-directory required\n"<< cmdline_desc() << HT_END;
        exit(1);
      }
    }
  };

  typedef Meta::list<MyPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

  set<String> include_paths;
  set<String> excludes;
  String start_key;
  String end_key;
  String toplevel_dir;
  SessionPtr hyperspace;
  String outdir;
  StringSet metadata_exclude;
  ofstream recovered_txt;
  uint64_t cumulative_size_so_far = 0;
  uint64_t cumulative_size_estimate = 0;
  uint64_t cumulative_size_divisor = 1;
  time_t cutoff_time = std::numeric_limits<time_t>::max();
  RE2 *path_regex = 0;
  boost::progress_display *progress = 0;

  bool verbose = false;
  bool dry_run;
  bool gzip;

  String format_time(time_t t) {
    char outstr[32];
    struct tm tm_val;

    localtime_r(&t, &tm_val);

    if (strftime(outstr, sizeof(outstr), "%F %T", &tm_val) == 0) {
      cout << "strftime returned 0" << endl;
      exit(-1);
    }
    return String(outstr);
  }

  void recover_directory(boost::iostreams::filtering_ostream &out, vector<String> &column_id_names, vector<bool> &column_is_counter, vector<String> &comps) {
    String path = "/hypertable/tables";
    ScanContextPtr scan_ctx(new ScanContext());
    std::string table_id;
    bool got_one = false;
    bool hit_start = start_key.empty();

    // Compute table ID
    {
      bool first = true;
      for (auto & comp : comps) {
        if (first)
          first = false;
        else
          table_id += "/";
        table_id += comp;
      }
    }

    MergeScannerRange mscanner(table_id, scan_ctx);

    path += "/" + table_id;

    if (verbose)
      cout << "Recovering " << path << endl;

    vector<Filesystem::Dirent> listing;
    Global::dfs->readdir(path, listing);
    size_t saved_len = comps.size();
    for (size_t i=0; i<listing.size(); i++) {
      if (!strncmp(listing[i].name.c_str(), "_xfer", 5) ||
	  !strcmp(listing[i].name.c_str(), "hints"))
	continue;
      else if (!strncmp(listing[i].name.c_str(), "cs", 2)) {
        String filename = path + "/" + listing[i].name;
        if (path_regex && !RE2::PartialMatch(filename.c_str(), *path_regex)) {
          if (verbose)
            cout << "Skipping " << filename << " because it doesn't match path regex\n";
          continue;
        }
	if (metadata_exclude.count(filename) > 0) {
          if (verbose)
	    cout << "Skipping " << filename << " because in metadata exclude\n";
	  continue;
	}
	if (listing[i].last_modification_time >= cutoff_time) {
          if (verbose)
	    cout << "Skipping " << filename << " because later than cutoff time\n";
	  continue;
	}
	cumulative_size_so_far += listing[i].length;
	recovered_txt << filename << "\n";
	if (dry_run)
          continue;
        try {
	  MergeScannerAccessGroup *ag_scanner = 
	    new MergeScannerAccessGroup(table_id, scan_ctx.get(),
					MergeScannerAccessGroup::RETURN_DELETES);
          CellStorePtr cellstore = CellStoreFactory::open(filename, 0, 0);
          CellListScannerPtr scanner = cellstore->create_scanner(scan_ctx.get());
	  ag_scanner->add_scanner(scanner);
          mscanner.add_scanner(ag_scanner);
          got_one = true;
        }
        catch (Exception &e) {
          HT_ERRORF("%s - %s", e.what(), Error::get_text(e.code()));
        }
      }
      else {
        comps.push_back(listing[i].name);
        recover_directory(out, column_id_names, column_is_counter, comps);
        comps.resize(saved_len);
      }
    }

    if (got_one) {
      ByteString value;
      Key key_comps;
      uint8_t *bsptr;
      size_t bslen;
      LoadDataEscape row_escaper;
      LoadDataEscape escaper;
      const char *escaped_buf, *row_escaped_buf;
      size_t escaped_len, row_escaped_len;
      DynamicBuffer value_buf;

      try {

        while (mscanner.get(key_comps, value)) {
          if (!hit_start) {
            if (strcmp(key_comps.row, start_key.c_str()) <= 0) {
              mscanner.forward();
              continue;
            }
            hit_start = true;
          }
          if (end_key.size()>0 &&  strcmp(key_comps.row, end_key.c_str()) > 0)
            break;
          row_escaper.escape(key_comps.row, strlen(key_comps.row),
                             &row_escaped_buf, &row_escaped_len);

          if (column_id_names[key_comps.column_family_code].length() > 0) {
            out << key_comps.timestamp << "\t" << row_escaped_buf << "\t" << column_id_names[key_comps.column_family_code];
            if (key_comps.column_qualifier && *key_comps.column_qualifier) {
              escaper.escape(key_comps.column_qualifier, key_comps.column_qualifier_len,
                             &escaped_buf, &escaped_len);
              out << ":" << escaped_buf;
            }

            bslen = value.decode_length((const uint8_t **)&bsptr);
            value_buf.ensure(bslen+1);
            memcpy(value_buf.base, bsptr, bslen);
            value_buf.base[bslen] = 0;
            if (column_is_counter[key_comps.column_family_code]) {
              if (bslen == 8 || bslen == 9) {
                const uint8_t *decode = value_buf.base;
                size_t remain = 8;
                int64_t count = Serialization::decode_i64(&decode, &remain);
                out << "\t" << count << "\n";
              }
              else
                out << "\t0\n";
            }
            else {
              escaper.escape((const char *)value_buf.base, bslen,
                             &escaped_buf, &escaped_len);
              out << "\t" << escaped_buf << "\n";
            }
          }

          mscanner.forward();
        }

      }
      catch (std::exception &e) {
        cerr << e.what() << endl;
      }
      if (progress) {
	unsigned long new_count = 
	  cumulative_size_so_far / cumulative_size_divisor;
	if (new_count > progress->count())
	  *progress += (new_count - progress->count());
      }
    }
  }

  void recover_table(vector<String> &ids, vector<String> &names) {
    Hyperspace::HandleCallbackPtr null_handle_callback;
    DynamicBuffer value_buf(0);
    String tablefile = toplevel_dir + "/tables";
    uint64_t handle = 0;

    for (size_t i=0; i<ids.size(); i++)
      tablefile += "/" + ids[i];

    try {
      if (hyperspace->exists(tablefile)) {
        handle = hyperspace->open(tablefile, OPEN_FLAG_READ, null_handle_callback);
        if (!hyperspace->attr_exists(handle, "x"))
          return;
      }
      else {
        cout << "Table " << tablefile << " does not exist" << endl;
        return;
      }

      hyperspace->attr_get(handle, "schema", value_buf);
      hyperspace->close(handle);
    }
    catch (Exception &e) {
      if (e.code() == Error::HYPERSPACE_FILE_NOT_FOUND ||
          e.code() == Error::HYPERSPACE_BAD_PATHNAME)
        return;
      HT_ERROR_OUT << e << HT_END;
      return;
    }

    String schema_buf((char *)value_buf.base, strlen((char *)value_buf.base));
    SchemaPtr schema(Schema::new_instance(schema_buf));
    value_buf.clear();

    String schema_str = schema->render_hql(names.back());

    String table_str;
    for (size_t i=0; i<names.size()-1; i++)
      table_str += "/" + names[i];
    String parent_path = outdir + table_str;

    //cout << "Recovering table " << table_str << "/" << names.back() << endl;

    FileUtils::mkdirs(parent_path);

    String schema_file_str = parent_path + "/create-" + names.back() + ".hql";
    ofstream schemafile;
    schemafile.open(schema_file_str.c_str());
    schemafile << schema_str << ";" << endl;
    schemafile.close();

    String tsv_file_str = parent_path + "/" + names.back() + ".tsv";
    boost::iostreams::filtering_ostream tsvfile;

    if (gzip) {
      tsvfile.push(boost::iostreams::gzip_compressor());
      tsv_file_str += ".gz";
    }

    tsvfile.push(boost::iostreams::file_descriptor_sink(tsv_file_str));

    vector<String> column_id_names;
    vector<bool> column_is_counter;
    column_id_names.resize(256);
    column_is_counter.resize(256);
    for (size_t i=0; i<256; i++)
      column_is_counter[i] = false;
    ColumnFamilySpecs &families = schema->get_column_families();
    for (auto & cf : families) {
      if (!cf->get_deleted())
        column_id_names[cf->get_id()] = cf->get_name();
      column_is_counter[cf->get_id()] = cf->get_option_counter();
    }

    recover_directory(tsvfile, column_id_names, column_is_counter, ids);

    tsvfile << std::flush;

  }

  void recover_namespace(vector<String> &ids, vector<String> &names) {
    Hyperspace::HandleCallbackPtr null_handle_callback;
    String idpath, namepath;

    for (size_t i=0; i<ids.size(); i++) {
      idpath += String("/") + ids[i];
      namepath += String("/") + names[i];
    }

    if (excludes.count(namepath) > 0)
      return;

    if (!namepath.empty() && !include_paths.empty() && include_paths.count(namepath) == 0)
      return;

    uint64_t handle = hyperspace->open(toplevel_dir + "/namemap/ids" + idpath,
        OPEN_FLAG_READ, null_handle_callback);

    std::vector<DirEntryAttr> listing;
    hyperspace->readdir_attr(handle, "name", false, listing);
    for (size_t i=0; i<listing.size(); i++) {
      if (!listing[i].has_attr) {
        cout << "Skipping " << listing[i].name << " because has no 'name' attribute ..." << "\n";
        continue;
      }
      if (listing[i].is_dir) {
        size_t saved_len = ids.size();
        ids.push_back(listing[i].name);
        names.push_back(String((const char *)listing[i].attr.base));
        recover_namespace(ids, names);
        ids.resize(saved_len);
        names.resize(saved_len);
      }
      else {
	String table_path = namepath + "/" + (const char *)listing[i].attr.base;
        if (excludes.count(table_path) == 0 &&
	    (include_paths.empty() || include_paths.count(table_path) > 0)) {
          size_t saved_len = ids.size();
          ids.push_back(listing[i].name);
          names.push_back(String((const char *)listing[i].attr.base));
          recover_table(ids, names);
          ids.resize(saved_len);
          names.resize(saved_len);
        }
      }
    }

    hyperspace->close(handle);
  }

  void load_metadata_exclude(const String &metadata_tsv) {
    String file_contents = FileUtils::file_to_string(metadata_tsv);
    String filepath;

    char_separator<char> sep_outer("\n");
    char_separator<char> sep_inner("\t");
    char_separator<char> sep_field(";");
    tokenizer<char_separator<char> > tokens_outer(file_contents, sep_outer);
    for (string line : tokens_outer) {
      tokenizer<char_separator<char> > tokens_inner(line, sep_inner);
      tokenizer<char_separator<char> >::iterator iter = tokens_inner.begin();
      if (iter == tokens_inner.end())
        continue;
      ++iter;
      if (iter == tokens_inner.end() ||
          iter->find_first_of("Files:") != 0)
        continue;
      ++iter;
      if (iter == tokens_inner.end())
        continue;
      tokenizer<char_separator<char> > tokens_field(*iter, sep_field);
      for (string file : tokens_field) {
        if (file.find_first_of("\\n") == 0)
          file = file.substr(2);
        if (!file.empty()) {
          filepath.clear();
          filepath.append("/hypertable/tables/");
          filepath.append(file);
          metadata_exclude.insert(filepath);
        }
      }
    }
  }


} // local namespace


int main(int argc, char **argv) {
  try {
    init_with_policies<Policies>(argc, argv);

    String host = get_str("FsBroker.Host");
    ::uint16_t port = get_i16("FsBroker.Port");
    ::uint32_t timeout_ms;
    String metadata_tsv;

    if (has("timeout"))
      timeout_ms = get_i32("timeout");
    else
      timeout_ms = get_i32("Hypertable.Request.Timeout");
    outdir = get_str("out-dir");
    boost::trim_right_if(outdir, boost::is_any_of("/"));

    time_t now = time(0);

    cout << format_time(now) << " - Current Time" <<  endl;

    if (has("exclude-metadata-since")) {
      time_t delta = get_i32("exclude-metadata-since");
      cutoff_time = now - delta;
      cout << format_time(cutoff_time) << " - Cutoff Time" << endl;
      Client *client = new Hypertable::Client();
      CommandInterpreterPtr interp(new HqlCommandInterpreter(client));
      interp->set_silent(true);
      interp->execute_line("USE sys");
      interp->execute_line("SELECT Files FROM METADATA REVS 1 INTO FILE 'metadata.tsv'");
      interp = 0;
      delete client;
      load_metadata_exclude("metadata.tsv");
    }

    Global::dfs = std::make_shared<FsBroker::Lib::Client>(host, port, timeout_ms);
    Global::memory_tracker = new MemoryTracker(0, 0);

    hyperspace = std::make_shared<Hyperspace::Session>(Comm::instance(), properties);

    Timer timer(timeout_ms, true);

    uint32_t remaining = timer.remaining();
    uint32_t interval=5000;
    uint32_t wait_time = (remaining < interval) ? remaining : interval;

    while (!hyperspace->wait_for_connection(wait_time)) {
      if (timer.expired())
        HT_THROW_(Error::CONNECT_ERROR_HYPERSPACE);

      cout << "Waiting for connection to Hyperspace..." << endl;

      remaining = timer.remaining();
      wait_time = (remaining < interval) ? remaining : interval;
    }

    if (has("verbose"))
      verbose = get_bool("verbose");
    dry_run = has("dry-run");
    gzip = has("gzip");

    if (has("start-key"))
      start_key = get_str("start-key");
    if (has("end-key"))
      end_key = get_str("end-key");

    if (has("path-regex")) {
      String regex = get_str("path-regex");
      boost::trim_if(regex, boost::is_any_of("'\""));
      cout << "Initializing regex=" << regex<< endl;
      path_regex = new RE2(regex.c_str());
    }

    if (has("include")) {
      std::vector<String> include_vec;
      String str = get_str("include", String());
      split(include_vec, str, is_any_of(","));
      std::vector<String> components;
      for (String path : include_vec) {
	components.clear();
	split(components, path, is_any_of("/"));
	String tmp_path;
	for (const auto &component : components) {
	  if (!component.empty()) {
	    tmp_path += String("/") + component;
	    include_paths.insert(tmp_path);
	  }
	}
      }
    }

    if (has("exclude")) {
      std::vector<String> exclude_vec;
      String str = get_str("exclude", String());
      split(exclude_vec, str, is_any_of(","));
      for (size_t i=0; i<exclude_vec.size(); i++) {
	if (exclude_vec[i][0] != '/')
	  excludes.insert("/" + exclude_vec[i]);
	else
	  excludes.insert(exclude_vec[i]);
      }
    }
    excludes.insert("/sys");
    excludes.insert("/tmp");

    if (has("cumulative-size-estimate")) {
      cumulative_size_estimate = get_i64("cumulative-size-estimate");
      if (cumulative_size_estimate > 1024*1024*1024)
	cumulative_size_divisor = 10*1024*1024;
      progress = new boost::progress_display((size_t)(cumulative_size_estimate /
						      cumulative_size_divisor));
    }

    toplevel_dir = properties->get_str("Hypertable.Directory");
    boost::trim_if(toplevel_dir, boost::is_any_of("/"));
    toplevel_dir = String("/") + toplevel_dir;

    vector<String> ids;
    vector<String> names;

    recovered_txt.open("recovered.txt");

    Stopwatch stopwatch;

    recover_namespace(ids, names);

    recovered_txt.close();
    cout << "\nElapsed time:  " << (int)stopwatch.elapsed() << " seconds\n";
    cout << "Cumulative Size = " << cumulative_size_so_far << endl;

    delete progress;

    hyperspace=0;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  return 0;
}
