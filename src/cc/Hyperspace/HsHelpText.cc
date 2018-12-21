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
#include "HsHelpText.h"

#include <Common/StringExt.h>

#include <unordered_map>

using namespace Hyperspace;

namespace {

  const char *help_text_contents[] = {
    "",
    "mkdir ............. Creates a directory in Hyperspace",
    "mkdirs ............ Creates all paths leading upto and including directory in Hyperspace",
    "delete ............ Delete file/directory",
    "open .............. Open a file/directory",
    "create ............ Create a file",
    "close ............. Close previously opened file/directory",
    "attrset ........... Set an attribute (key/value pair) for a file/directory",
    "attrget ........... Retrieve an attribute for a file/directory",
    "attrincr........... Atomically increment the value of an attribute (assumed uint64)",
    "attrexists ........ Check if a particular attribute is set for a file/directory",
    "attrlist .......... Retrieve all attributes (keys only) for a file/directory",
    "attrdel ........... Delete an attribure for a file/directory",
    "exists ............ Check if a file/directory exists",
    "readdir ........... List the contents of a previously opened directory",
    "readdirattr ....... List the contents of a previously opened directory which has",
    "            ....... the named attribute set and the value of the attribute",
    "readpathattr ...... List the values of an attribute for each path component of a",
    "            ....... previously opened file/directory",
    "lock .............. Lock access to a file/directory",
    "trylock ........... Check whether a lock on file/directory would succeed",
    "release ........... Release previously acquired lock",
    "getseq ............ Get a lock sequencer for a file/directory",
    "echo .............. Echo user input",
    "locate ............ Get the location of Hyperspace Master or all Replicas",
    "dump .............. Dump contents of hyperspace",
    "status ............ Get server status",
    "cfg_reload ........ Reload configurations file"
    "",
    "Statements must be terminated with ';' to execute.  For more information on",
    "a specific statement, type 'help <statement>', where <statement> is one from",
    "the preceeding list.",
    "",
    0
  };

  const char *help_mkdir[] = {
    "mkdir <dir>",
    "  This command issues a MKDIR request to Hyperspace.",
    (const char *)0
  };

  const char *help_mkdirs[] = {
    "mkdirs <dir>",
    "  This command issues a MKDIRS request to Hyperspace.",
    (const char *)0
  };


  const char *help_delete[] = {
    "delete <dir>",
    "  This command issues a DELETE request to Hyperspace.",
    (const char *)0
  };

  const char *help_open[] = {
    "open <fname> flags=[READ|WRITE|LOCK|CREATE|EXCL|TEMP|LOCK_SHARED|LOCK_EXCLUSIVE] [event-mask=<mask>]",
    "  This command issues an OPEN request to Hyperspace.  The optional",
    "  parameter event-mask may take a value that is the combination of",
    "  the following strings:",
    "    ATTR_SET|ATTR_DEL|CHILD_NODE_ADDED|CHILD_NODE_REMOVED|LOCK_ACQUIRED|LOCK_RELEASED",
    (const char *)0
  };

  const char *help_create[] = {
    "create <fname> flags=[READ|WRITE|LOCK|TEMP|LOCK_SHARED|LOCK_EXCLUSIVE] [OPTIONS]",
    "OPTIONS:",
    "  attr:<name>=<value>  This can be used to specify extended attributes that should",
    "                       get created atomically when the file is created",
    "  event-mask=<mask>  This indicates which events should be delivered on this handle.",
    "                     The <mask> value can take any combination of the following:",
    "                     ATTR_SET|ATTR_DEL|LOCK_ACQUIRED|LOCK_RELEASED",
    "  This command issues an CREATE request to Hyperspace.",
    (const char *)0
  };

  const char *help_close[] = {
    "close <file>",
    "  This command issues a CLOSE request to Hyperspace.",
    (const char *)0
  };

  const char *help_attrset[] = {
    "attrset <file> <name>=<value> [i16|i32|i64]",
    "  This command issues a ATTRSET request to Hyperspace.",
    (const char *)0
  };

  const char *help_attrget[] = {
    "attrget <file> <name> [int|short|long]",
    "  This command issues a ATTRGET request to Hyperspace.",
    (const char *)0
  };

  const char *help_attrincr[] = {
    "attrincr <file> <name> ",
    "  This command issues a ATTRINCR request to Hyperspace.",
    (const char *)0
  };


  const char *help_attrexists[] = {
    "attrexists <file> <name>",
    "  This command issues a ATTREXISTS request to Hyperspace.",
    (const char *)0
  };

  const char *help_attrlist[] = {
    "attrlist <file> "
    "  This command issues a ATTRLIST request to Hyperspace.",
    (const char *)0
  };

  const char *help_attrdel[] = {
    "attrdel <file> <name>",
    "  This command issues a ATTRDEL request to Hyperspace.",
    (const char *)0
  };

  const char *help_exists[] = {
    "exists <name>",
    "  This command issues a EXISTS request to Hyperspace.",
    (const char *)0
  };

  const char *help_readdir[] = {
    "readdir <dir>",
    "  This command issues a READDIR request to Hyperspace.",
    (const char *)0
  };

  const char *help_readdirattr[] = {
    "readdirattr [-r] <dir> <name>",
    "  This command issues a READDIRATTR request to Hyperspace.",
    (const char *)0
  };

  const char *help_readpathattr[] = {
    "readpathattr <dir> <name>",
    "  This command issues a READPATHATTR request to Hyperspace.",
    (const char *)0
  };


  const char *help_lock[] = {
    "lock <file> <mode>",
    "  This command issues a LOCK request to Hyperspace.  The <mode> argument",
    "  may be either SHARED or EXCLUSIVE.",
    (const char *)0
  };

  const char *help_trylock[] = {
    "trylock <file> <mode>",
    "  This command issues a TRYLOCK request to Hyperspace.  The <mode> argument",
    "  may be either SHARED or EXCLUSIVE.",
    (const char *)0
  };

  const char *help_release[] = {
    "release <file>",
    "  This command issues a RELEASE request to Hyperspace which causes any",
    "  lock held by this handle to be released.",
    (const char *)0
  };

  const char *help_getsequencer[] = {
    "getseq <file>",
    "  This command gets the lock sequencer for the given file.",
    (const char *)0
  };

  const char *help_locate[] = {
    "locate master|replicas",
    "  This command prints out either the location of the Hyperspace replica "
    "  which is the current replication master or the locations of all Hyperspace replicas",
    (const char *)0
  };
  
  const char *help_cfg_reload[] = {
    "cfg_reload Null | ConfigFileFullPath",
    "  This command reloads a new configuration file, default eg. /opt/hypertable/current/conf/hypertable.cfg"
    "  else the specified file path",
    (const char *)0
  };

  const char *help_dump[] = {
    "dump <path> [AS_COMMANDS] [output_file]",
    "  This command dumps all the contents of Hyperspace under the specified path.",
    "  If no output_file is specified the contents are dumped to STDOUT.",
    "  The AS_COMMANDS option prints out the contents as Hyperspace shell commands ",
    "  for easy reloading back into Hyperspace. To reload while preserving white spaces",
    "  in attributes, use something like 'hyperspace --command-file dump_file'.",
    (const char *)0
  };


  const char *help_status[] = {
    "status",
    "",
    "  This command sends a status request to Hyperspace, printing",
    "  the status output message to the console and returning the status code.",
    "  The return value of the last command issued to the interpreter will be",
    "  used as the exit status.",
    nullptr
  };

  typedef std::unordered_map<std::string, const char **> HelpTextMap;

  HelpTextMap &build_help_text_map() {
    HelpTextMap *map = new HelpTextMap();
    (*map)[""] = help_text_contents;
    (*map)["mkdir"] = help_mkdir;
    (*map)["mkdirs"] = help_mkdirs;
    (*map)["delete"] = help_delete;
    (*map)["open"] = help_open;
    (*map)["create"] = help_create;
    (*map)["close"] = help_close;
    (*map)["attrset"] = help_attrset;
    (*map)["attrget"] = help_attrget;
    (*map)["attrincr"] = help_attrincr;
    (*map)["attrexists"] = help_attrexists;
    (*map)["attrlist"] = help_attrlist;
    (*map)["attrdel"] = help_attrdel;
    (*map)["exists"] = help_exists;
    (*map)["readdir"] = help_readdir;
    (*map)["readdirattr"] = help_readdirattr;
    (*map)["readpathattr"] = help_readpathattr;
    (*map)["lock"] = help_lock;
    (*map)["trylock"] = help_trylock;
    (*map)["release"] = help_release;
    (*map)["getseq"] = help_getsequencer;
    (*map)["locate"] = help_locate;
    (*map)["cfg_reload"] = help_cfg_reload;
    (*map)["dump"] = help_dump;
    (*map)["status"] = help_status;
    return *map;
  }

  HelpTextMap &text_map = build_help_text_map();
}


const char **HsHelpText::get(const std::string &subject) {
  HelpTextMap::const_iterator iter = text_map.find(subject);
  if (iter == text_map.end())
    return 0;
  return (*iter).second;
}

