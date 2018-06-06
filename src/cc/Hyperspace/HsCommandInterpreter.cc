/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#include "HsCommandInterpreter.h"
#include "HsHelpText.h"
#include "HsParser.h"
#include "DirEntry.h"
#include "DirEntryAttr.h"
#include "FileHandleCallback.h"

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Stopwatch.h>
#include <Common/DynamicBuffer.h>

#include <boost/algorithm/string.hpp>

extern "C" {
#if defined(__sun__)
#include <inttypes.h>
#endif
}

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace std;
using namespace Hyperspace;
using namespace HsParser;


HsCommandInterpreter::HsCommandInterpreter(Session* session)
    : m_session(session) {
}


int HsCommandInterpreter::execute_line(const String &line) {
  String out_str;
  ParserState state;
  Parser parser(state);
  parse_info<> info;

  info = parse(line.c_str(), parser, space_p);

  if (info.full) {

    if (state.command == COMMAND_MKDIR) {
      String fname = state.dir_name;
      m_session->mkdir(fname);
    }
    else if (state.command == COMMAND_MKDIRS) {
      String fname = state.dir_name;
      m_session->mkdirs(fname);
    }
    else if (state.command == COMMAND_DELETE) {
      String fname = state.node_name;
      m_session->unlink(fname);
    }

    else if (state.command == COMMAND_OPEN) {
      ::uint64_t handle;
      int event_mask = state.event_mask;
      int open_flag = state.open_flag;
      String fname = state.node_name;

      if(open_flag == 0) // read is default
        open_flag = OPEN_FLAG_READ;

      if(fname == "")
        HT_THROW(Error::HYPERSPACE_CLI_PARSE_ERROR,
                 "Error: no filename supplied.");

      HandleCallbackPtr callback = std::make_shared<FileHandleCallback>(event_mask);
      handle = m_session->open(fname,open_flag,callback);

      // store opened handle in global HsClientState namespace
      String normal_name;
      Util::normalize_pathname(fname,normal_name);
      HsClientState::file_map[normal_name] = handle;
    }

    else if (state.command == COMMAND_CREATE) {
      ::uint64_t handle;
      int event_mask = state.event_mask;
      int open_flag = state.open_flag;
      String fname = state.file_name;

      if(open_flag == 0)
        HT_THROW(Error::HYPERSPACE_CLI_PARSE_ERROR,
                 "Error: no flags supplied.");
      else if (fname == "")
        HT_THROW(Error::HYPERSPACE_CLI_PARSE_ERROR,
                 "Error: no filename supplied.");

      HandleCallbackPtr callback = std::make_shared<FileHandleCallback>(event_mask);
      handle = m_session->create(fname,open_flag,callback,state.attrs);

      // store opened handle in global HsClientState namespace
      String normal_name;
      Util::normalize_pathname(fname,normal_name);
      HsClientState::file_map[normal_name] = handle;
    }

    else if (state.command == COMMAND_CLOSE) {
      ::uint64_t handle;
      String fname = state.node_name;
      handle = Util::get_handle(fname);
      String normal_name;
      Util::normalize_pathname(fname,normal_name);
      HsClientState::file_map.erase(normal_name);
      m_session->close(handle);
    }

    else if (state.command == COMMAND_ATTRSET) {
      ::uint64_t handle = 0;
      int size = state.last_attr_size;
      String name = state.last_attr_name;
      String value = state.last_attr_value;
      String fname = state.node_name;
      const char *val = value.c_str();

      try {
        handle = Util::get_handle(fname);
        m_session->attr_set(handle, name, val, size);
      }
      catch (Exception &e) {
        String normalized_fname;
        Util::normalize_pathname(fname, normalized_fname);
        m_session->attr_set(normalized_fname, name, val, size);
      }
    }

    else if (state.command == COMMAND_ATTRGET) {
      ::uint64_t handle;
      String name = state.last_attr_name;
      String fname = state.node_name;
      DynamicBuffer value(0);

      try {
        handle = Util::get_handle(fname);
        m_session->attr_get(handle, name, value);
      }
      catch (Exception &e) {
        String normalized_fname;
        Util::normalize_pathname(fname, normalized_fname);
        m_session->attr_get(normalized_fname, name, value);
      }

      String valstr = String((const char*)(value.base),value.fill());
      cout << valstr << endl;
    }

    else if (state.command == COMMAND_ATTRINCR) {
      ::uint64_t handle;
      String name = state.last_attr_name;
      String fname = state.node_name;
      uint64_t attr_val;

      try {
        handle = Util::get_handle(fname);
        attr_val = m_session->attr_incr(handle, name);
      }
      catch (Exception &e) {
        String normalized_fname;
        Util::normalize_pathname(fname, normalized_fname);
        attr_val = m_session->attr_incr(normalized_fname, name);
      }

      cout << attr_val << endl;
    }

    else if (state.command == COMMAND_ATTREXISTS) {
      ::uint64_t handle = 0;
      String name = state.last_attr_name;
      String fname = state.node_name;
      bool exists;

      try {
        handle = Util::get_handle(fname);
        exists = m_session->attr_exists(handle, name);
      }
      catch (Exception &e) {
        String normalized_fname;
        Util::normalize_pathname(fname, normalized_fname);
        exists = m_session->attr_exists(normalized_fname, name);
      }

      if (exists)
        cout << "true" << endl;
      else
        cout << "false" << endl;
    }

    else if (state.command == COMMAND_ATTRLIST) {
      ::uint64_t handle;
      String fname = state.node_name;
      vector<String> attrs;

      handle = Util::get_handle(fname);

      m_session->attr_list(handle, attrs);

      for (vector<String>::iterator it = attrs.begin(); it != attrs.end(); ++it) {
        cout << *it << endl;
      }
    }

    else if (state.command == COMMAND_ATTRDEL) {
      ::uint64_t handle;
      String name = state.last_attr_name;
      String fname = state.node_name;

      handle = Util::get_handle(fname);

      m_session->attr_del(handle, name);
    }

    else if (state.command == COMMAND_EXISTS) {
      String fname = state.node_name;

      if(m_session->exists(fname)) {
        cout<< "true" << endl;
      }
      else {
        cout << "false" << endl;
        HsClientState::exit_status = 1;
      }
    }

    else if (state.command == COMMAND_READDIR) {
      ::uint64_t handle;
      vector<struct DirEntry> listing;
      String fname = state.dir_name;

      handle = Util::get_handle(fname);
      m_session->readdir(handle, listing);

      struct LtDirEntry ascending;
      sort(listing.begin(), listing.end(), ascending);
      for (size_t ii=0; ii<listing.size(); ii++) {
        if (listing[ii].is_dir)
          cout << "(dir) ";
        else
          cout << "      ";
        cout << listing[ii].name << endl ;
      }
      cout << flush ;
    }

    else if (state.command == COMMAND_READDIRATTR) {
      ::uint64_t handle;
      vector<DirEntryAttr> listing;
      String fname = state.dir_name;
      String name = state.last_attr_name;

      handle = Util::get_handle(fname);
      m_session->readdir_attr(handle, name, state.recursive, listing);

      struct LtDirEntryAttr ascending;
      sort(listing.begin(), listing.end(), ascending);
      printDirEntryAttrListing(0, name, listing);
    }

    else if (state.command == COMMAND_READPATHATTR) {
      ::uint64_t handle;
      vector<DirEntryAttr> listing;
      String fname = state.dir_name;
      String name = state.last_attr_name;

      handle = Util::get_handle(fname);
      m_session->readpath_attr(handle, name, listing);

      struct LtDirEntryAttr ascending;
      sort(listing.begin(), listing.end(), ascending);
      printDirEntryAttrListing(0, name, listing);
    }

    else if (state.command == COMMAND_DUMP) {
      ::uint64_t handle;
      String fname;
      String outfile = state.outfile;
      String slash("/");
      vector<String> dirs;
      String last_component;
      ostream *oo;
      DynamicBuffer value(0);
      String value_str;
      String display_fname;
      String escaped_double_quote("\\\"");
      String unescaped_double_quote(1, '"');
      String escaped_single_quote("\\\'");
      String unescaped_single_quote("'");
      String escaped_newline("\\n\n");
      String unescaped_newline(1,'\n');

      String lock_attr("lock.generation");
      String generation_attr("generation");

      dirs.push_back(state.dir_name);
      if (!outfile.empty()) {
        FileUtils::expand_tilde(state.outfile);
        oo = new ofstream(state.outfile.c_str(), ios_base::out);
      }
      else
        oo = &cout;

      while (!dirs.empty()) {
        Util::normalize_pathname(dirs.back(), fname);
        if (fname.size()==0)
          display_fname = "/";
        else
          display_fname = fname;
        dirs.pop_back();
        vector<struct DirEntry> listing;
        vector<String> attrs;
        vector<String> tmp_dirs;
        HandleCallbackPtr null_callback;
        bool has_newline=false;

        handle = m_session->open(fname, OPEN_FLAG_READ, null_callback);

        if (state.as_commands) {
          *oo << "MKDIRS " << display_fname << ";" << endl;
          *oo << "OPEN " << display_fname << " FLAGS=READ|WRITE|CREATE ;" << endl;
        }
        else
          *oo << display_fname << "\t" << endl;
        // read all attrs of this dir
        attrs.clear();
        m_session->attr_list(handle, attrs);

        for (const auto &attr : attrs) {
          if (attr == lock_attr || attr == generation_attr)
            continue;
          value.clear();
          m_session->attr_get(handle, attr, value);
          value_str = String((const char *)value.base, value.fill());

          boost::algorithm::replace_all(value_str, unescaped_double_quote, escaped_double_quote);
          boost::algorithm::replace_all(value_str, unescaped_single_quote, escaped_single_quote);
          has_newline = false;
          if (value_str.find("\n") != String::npos)
            has_newline = true;
          //boost::algorithm::replace_all(value_str, unescaped_newline, escaped_newline);

          if (state.as_commands) {
            *oo << "ATTRSET " << display_fname << " " << attr << "=\"" << value_str << "\"";
            if (has_newline)
              *oo << "\n";
            *oo << ";" << endl;
          }
          else
            *oo << display_fname << ":" << attr << "\t" << value_str << endl;
        }

        // read all files and dirs under this path; skip BAD_PATHNAME errors. 
        // they simply mean that the path is a file and not a directory
        try {
          m_session->readdir(handle, listing);
        }
        catch (Exception &e) {
          if (e.code() != Error::HYPERSPACE_BAD_PATHNAME)
            throw;
          else
            continue;
        }
        for (const auto &entry : listing) {
          // if dir save it
          if(entry.is_dir)
            tmp_dirs.push_back(fname + "/" + entry.name);
          // if file get all get all info
          else {
            String file;
            Util::normalize_pathname(fname + "/" + entry.name, file);

            if (state.as_commands)
              *oo << "OPEN " << file << " FLAGS=READ|WRITE|CREATE ;" << endl;
            else
              *oo << file << "\t" << endl;
            handle = m_session->open(file, OPEN_FLAG_READ, null_callback);
            attrs.clear();
            m_session->attr_list(handle, attrs);

            for (const auto &attr : attrs) {
              if (attr == lock_attr || attr == generation_attr)
                continue;
              value.clear();
              m_session->attr_get(handle, attr, value);
              value_str = String((const char *)value.base, value.fill());
              boost::algorithm::replace_all(value_str, unescaped_double_quote, escaped_double_quote);
              boost::algorithm::replace_all(value_str, unescaped_single_quote, escaped_single_quote);
              has_newline = false;
              if (value_str.find("\n") != String::npos)
                has_newline = true;
              //boost::algorithm::replace_all(value_str, unescaped_newline, escaped_newline);

              if (state.as_commands) {
                *oo << "ATTRSET " << file << " " << attr << "=\"" << value_str << "\"";
                if (has_newline)
                  *oo << "\n";
                *oo << ";" << endl;
              }
              else
                *oo << file << ":" << attr << "\t" << value_str << endl;
            }
          }
        }

        if (tmp_dirs.size())
          for(int ii=tmp_dirs.size()-1; ii>=0; --ii)
            dirs.push_back(tmp_dirs[ii]);
      }
    }

    else if (state.command == COMMAND_LOCK) {
      ::uint64_t handle;
      LockMode mode = (LockMode)state.lock_mode;
      String fname = state.node_name;
      struct LockSequencer lockseq;

      handle = Util::get_handle(fname);

      m_session->lock(handle, mode, &lockseq);

      cout << "SEQUENCER name=" << lockseq.name << " mode=" << lockseq.mode
           << " generation=" << lockseq.generation << endl;
    }

    else if (state.command == COMMAND_TRYLOCK) {
      ::uint64_t handle;
      LockMode mode = (LockMode)state.lock_mode;
      String fname = state.node_name;
      struct LockSequencer lockseq;
      LockStatus status;

      handle = Util::get_handle(fname);
      m_session->try_lock(handle, mode, &status, &lockseq);

      if (status == LOCK_STATUS_GRANTED)
        cout << "SEQUENCER name=" << lockseq.name << " mode=" << lockseq.mode
             << " generation=" << lockseq.generation << endl;
      else if (status == LOCK_STATUS_BUSY)
        cout << "busy" << endl;
      else
        cout << "Unknown status: " << status << endl;

    }

    else if (state.command == COMMAND_RELEASE) {
      ::uint64_t handle;
      String fname = state.node_name;

      handle = Util::get_handle(fname);

      m_session->release(handle);
    }

    else if (state.command == COMMAND_GETSEQ) {
      ::uint64_t handle;
      struct LockSequencer lockseq;
      String fname = state.node_name;

      handle = Util::get_handle(fname);

      m_session->get_sequencer(handle, &lockseq);

      cout << "SEQUENCER name=" << lockseq.name << " mode=" << lockseq.mode
           << " generation=" << lockseq.generation << endl;
    }

    else if (state.command == COMMAND_ECHO) {
      String str = state.str;

      cout << str << endl;
    }

    else if (state.command == COMMAND_LOCATE) {
      int type = state.locate_type;
      String result = m_session->locate(type);
      cout << result << endl;
    }

    else if (state.command == COMMAND_STATUS) {
      Status status;
      Status::Code code;
      int32_t error = m_session->status(status);
      if (error == Error::OK) {
        string text;
        status.get(&code, text);
        if (!m_silent) {
          cout << "Hyperspace " << Status::code_to_string(code);
          if (!text.empty())
            cout << " - " << text;
          cout << endl;
        }
      }
      else {
        if (!m_silent)
          cout << "Hyperspace CRITICAL - " << Error::get_text(error) << endl;
        code = Status::Code::CRITICAL;
      }
      return static_cast<int>(code);
    }

    else if (state.command == COMMAND_HELP) {
      const char **text = HsHelpText::get(state.help_str);

      if (text) {
        for (size_t i=0; text[i]; i++)
          cout << text[i] << endl;
      }
      else
        cout << endl << "no help for '" << state.help_str << "'\n" << endl;
    }

    else
      HT_THROW(Error::HYPERSPACE_CLI_PARSE_ERROR, "unsupported command: "
               + line);
  }
  else
    HT_THROWF(Error::HYPERSPACE_CLI_PARSE_ERROR, "parse error at: %s",
              info.stop);
  return 0;
}

void HsCommandInterpreter::printDirEntryAttrListing(int indent, const String& attr_name, const std::vector<DirEntryAttr> listing) {
  static const int indent_size = 2;
  for (size_t ii=0; ii<listing.size(); ii++) {
    if (listing[ii].is_dir)
      cout << "(dir) ";
    else
      cout << "      ";
    if (indent) cout << String(indent - indent_size, ' ') << "+" << String(indent_size - 1, ' ');
    if (listing[ii].has_attr) {
      String attr_val((const char*)listing[ii].attr.base);
      cout << listing[ii].name << ", " << attr_name << "=" << attr_val << endl ;
    }
    else
      cout << listing[ii].name << endl ;

    printDirEntryAttrListing(indent + indent_size, attr_name, listing[ii].sub_entries);
  }
  cout << flush ;
}
