/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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
#include "Common/HashMap.h"

#include "Error.h"

#include <iomanip>

namespace Hypertable { namespace Logger {
    extern bool show_line_numbers;
  }
}

using namespace Hypertable;

namespace {
  struct ErrorInfo {
    int          code;
    const char  *text;
  };

  ErrorInfo error_info[] = {
    { Error::UNPOSSIBLE,                  "But that's unpossible!" },
    { Error::EXTERNAL,                    "External error" },
    { Error::FAILED_EXPECTATION,          "HYPERTABLE failed expectation" },
    { Error::OK,                          "HYPERTABLE ok" },
    { Error::PROTOCOL_ERROR,              "HYPERTABLE protocol error" },
    { Error::REQUEST_TRUNCATED,           "HYPERTABLE request truncated" },
    { Error::RESPONSE_TRUNCATED,          "HYPERTABLE response truncated" },
    { Error::REQUEST_TIMEOUT,             "HYPERTABLE request timeout" },
    { Error::LOCAL_IO_ERROR,              "HYPERTABLE local i/o error" },
    { Error::BAD_ROOT_LOCATION,           "HYPERTABLE bad root location" },
    { Error::BAD_SCHEMA,                  "HYPERTABLE bad schema" },
    { Error::INVALID_METADATA,            "HYPERTABLE invalid metadata" },
    { Error::BAD_KEY,                     "HYPERTABLE bad key" },
    { Error::METADATA_NOT_FOUND,          "HYPERTABLE metadata not found" },
    { Error::HQL_PARSE_ERROR,             "HYPERTABLE HQL parse error" },
    { Error::FILE_NOT_FOUND,              "HYPERTABLE file not found" },
    { Error::BLOCK_COMPRESSOR_UNSUPPORTED_TYPE,
        "HYPERTABLE block compressor unsupported type" },
    { Error::BLOCK_COMPRESSOR_INVALID_ARG,
        "HYPERTABLE block compressor invalid arg" },
    { Error::BLOCK_COMPRESSOR_TRUNCATED,
        "HYPERTABLE block compressor block truncated" },
    { Error::BLOCK_COMPRESSOR_BAD_HEADER,
        "HYPERTABLE block compressor bad block header" },
    { Error::BLOCK_COMPRESSOR_BAD_MAGIC,
        "HYPERTABLE block compressor bad magic string" },
    { Error::BLOCK_COMPRESSOR_CHECKSUM_MISMATCH,
        "HYPERTABLE block compressor block checksum mismatch" },
    { Error::BLOCK_COMPRESSOR_DEFLATE_ERROR,
        "HYPERTABLE block compressor deflate error" },
    { Error::BLOCK_COMPRESSOR_INFLATE_ERROR,
        "HYPERTABLE block compressor inflate error" },
    { Error::BLOCK_COMPRESSOR_INIT_ERROR,
        "HYPERTABLE block compressor initialization error" },
    { Error::TABLE_NOT_FOUND,             "HYPERTABLE table does not exist" },
    { Error::COMMAND_PARSE_ERROR,         "HYPERTABLE command parse error" },
    { Error::CONNECT_ERROR_MASTER,        "HYPERTABLE Master connect error" },
    { Error::CONNECT_ERROR_HYPERSPACE,
        "HYPERTABLE Hyperspace client connect error" },
    { Error::TOO_MANY_COLUMNS,            "HYPERTABLE too many columns" },
    { Error::BAD_DOMAIN_NAME,             "HYPERTABLE bad domain name" },
    { Error::MALFORMED_REQUEST,           "HYPERTABLE malformed request" },
    { Error::BAD_MEMORY_ALLOCATION,       "HYPERTABLE bad memory allocation"},
    { Error::BAD_SCAN_SPEC,               "HYPERTABLE bad scan specification"},
    { Error::NOT_IMPLEMENTED,             "HYPERTABLE not implemented"},
    { Error::VERSION_MISMATCH,            "HYPERTABLE version mismatch"},
    { Error::CANCELLED,                   "HYPERTABLE cancelled"},
    { Error::SCHEMA_PARSE_ERROR,          "HYPERTABLE schema parse error" },
    { Error::SYNTAX_ERROR,                "HYPERTABLE syntax error" },
    { Error::DOUBLE_UNGET,                  "HYPERTABLE double unget" },
    { Error::EMPTY_BLOOMFILTER,             "HYPERTABLE empty bloom filter" },
    { Error::BLOOMFILTER_CHECKSUM_MISMATCH, "HYPERTABLE bloom filter checksum mismatch" },
    { Error::NAME_ALREADY_IN_USE,           "HYPERTABLE name already in use" },
    { Error::NAMESPACE_DOES_NOT_EXIST,      "HYPERTABLE namespace does not exist" },
    { Error::BAD_NAMESPACE,                 "HYPERTABLE bad namespace" },
    { Error::NAMESPACE_EXISTS,              "HYPERTABLE namespace exists" },
    { Error::NO_RESPONSE,                   "HYPERTABLE no response" },
    { Error::NOT_ALLOWED,                   "HYPERTABLE not allowed" },
    { Error::INDUCED_FAILURE,               "HYPERTABLE induced failure" },
    { Error::SERVER_SHUTTING_DOWN,          "HYPERTABLE server shutting down" },
    { Error::LOCATION_UNASSIGNED,           "HYPERTABLE location unassigned" },
    { Error::ALREADY_EXISTS,                "HYPERTABLE cell already exists" },
    { Error::CHECKSUM_MISMATCH,             "HYPERTABLE checksum mismatch" },
    { Error::CLOSED,                        "HYPERTABLE closed" },
    { Error::RANGESERVER_NOT_FOUND,         "HYPERTABLE RangeServer not found" },
    { Error::CONNECTION_NOT_INITIALIZED,    "HYPERTABLE connection not initialized" },
    { Error::CONFIG_BAD_ARGUMENT,         "CONFIG bad argument(s)"},
    { Error::CONFIG_BAD_CFG_FILE,         "CONFIG bad cfg file"},
    { Error::CONFIG_GET_ERROR,            "CONFIG failed to get config value"},
    { Error::CONFIG_BAD_VALUE,            "CONFIG bad config value"},
    { Error::COMM_NOT_CONNECTED,          "COMM not connected" },
    { Error::COMM_BROKEN_CONNECTION,      "COMM broken connection" },
    { Error::COMM_CONNECT_ERROR,          "COMM connect error" },
    { Error::COMM_ALREADY_CONNECTED,      "COMM already connected" },
    { Error::COMM_SEND_ERROR,             "COMM send error" },
    { Error::COMM_RECEIVE_ERROR,          "COMM receive error" },
    { Error::COMM_POLL_ERROR,             "COMM poll error" },
    { Error::COMM_CONFLICTING_ADDRESS,    "COMM conflicting address" },
    { Error::COMM_SOCKET_ERROR,           "COMM socket error" },
    { Error::COMM_BIND_ERROR,             "COMM bind error" },
    { Error::COMM_LISTEN_ERROR,           "COMM listen error" },
    { Error::COMM_HEADER_CHECKSUM_MISMATCH,  "COMM header checksum mismatch" },
    { Error::COMM_PAYLOAD_CHECKSUM_MISMATCH, "COMM payload checksum mismatch" },
    { Error::COMM_BAD_HEADER,             "COMM bad header" },
    { Error::COMM_INVALID_PROXY,          "COMM invalid proxy" },
    { Error::DFSBROKER_BAD_FILE_HANDLE,   "DFS BROKER bad file handle" },
    { Error::DFSBROKER_IO_ERROR,          "DFS BROKER i/o error" },
    { Error::DFSBROKER_FILE_NOT_FOUND,    "DFS BROKER file not found" },
    { Error::DFSBROKER_BAD_FILENAME,      "DFS BROKER bad filename" },
    { Error::DFSBROKER_PERMISSION_DENIED, "DFS BROKER permission denied" },
    { Error::DFSBROKER_INVALID_ARGUMENT,  "DFS BROKER invalid argument" },
    { Error::DFSBROKER_INVALID_CONFIG,    "DFS BROKER invalid config value" },
    { Error::DFSBROKER_EOF,               "DFS BROKER end of file" },
    { Error::HYPERSPACE_IO_ERROR,         "HYPERSPACE i/o error" },
    { Error::HYPERSPACE_CREATE_FAILED,    "HYPERSPACE create failed" },
    { Error::HYPERSPACE_FILE_NOT_FOUND,   "HYPERSPACE file not found" },
    { Error::HYPERSPACE_ATTR_NOT_FOUND,   "HYPERSPACE attribute not found" },
    { Error::HYPERSPACE_DELETE_ERROR,     "HYPERSPACE delete error" },
    { Error::HYPERSPACE_BAD_PATHNAME,     "HYPERSPACE bad pathname" },
    { Error::HYPERSPACE_PERMISSION_DENIED,"HYPERSPACE permission denied" },
    { Error::HYPERSPACE_EXPIRED_SESSION,  "HYPERSPACE expired session" },
    { Error::HYPERSPACE_FILE_EXISTS,      "HYPERSPACE file exists" },
    { Error::HYPERSPACE_IS_DIRECTORY,     "HYPERSPACE is directory" },
    { Error::HYPERSPACE_INVALID_HANDLE,   "HYPERSPACE invalid handle" },
    { Error::HYPERSPACE_REQUEST_CANCELLED,"HYPERSPACE request cancelled" },
    { Error::HYPERSPACE_MODE_RESTRICTION, "HYPERSPACE mode restriction" },
    { Error::HYPERSPACE_ALREADY_LOCKED,   "HYPERSPACE already locked" },
    { Error::HYPERSPACE_LOCK_CONFLICT,    "HYPERSPACE lock conflict" },
    { Error::HYPERSPACE_NOT_LOCKED,       "HYPERSPACE not locked" },
    { Error::HYPERSPACE_BAD_ATTRIBUTE,    "HYPERSPACE bad attribute" },
    { Error::HYPERSPACE_BERKELEYDB_ERROR, "HYPERSPACE Berkeley DB error" },
    { Error::HYPERSPACE_DIR_NOT_EMPTY,    "HYPERSPACE directory not empty" },
    { Error::HYPERSPACE_BERKELEYDB_DEADLOCK,
        "HYPERSPACE Berkeley DB deadlock" },
    { Error::HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD,
        "HYPERSPACE Berkeley DB replication handle dead" },
    { Error::HYPERSPACE_FILE_OPEN,        "HYPERSPACE file open" },
    { Error::HYPERSPACE_CLI_PARSE_ERROR,  "HYPERSPACE CLI parse error" },
    { Error::HYPERSPACE_CREATE_SESSION_FAILED,
        "HYPERSPACE unable to create session " },
    { Error::HYPERSPACE_NOT_MASTER_LOCATION,
        "HYPERSPACE not master location" },

    { Error::HYPERSPACE_STATEDB_ERROR,
        "HYPERSPACE State DB error" },
    { Error::HYPERSPACE_STATEDB_DEADLOCK,
        "HYPERSPACE State DB deadlock" },
    { Error::HYPERSPACE_STATEDB_BAD_KEY,
        "HYPERSPACE State DB bad key" },
    { Error::HYPERSPACE_STATEDB_BAD_VALUE,
        "HYPERSPACE State DB bad value" },
    { Error::HYPERSPACE_STATEDB_ALREADY_DELETED,
        "HYPERSPACE State DB attempt to access/delete previously deleted state" },
    { Error::HYPERSPACE_STATEDB_EVENT_EXISTS,
        "HYPERSPACE State DB event exists" },
    { Error::HYPERSPACE_STATEDB_EVENT_NOT_EXISTS,
        "HYPERSPACE State DB event does not exist" },
    { Error::HYPERSPACE_STATEDB_EVENT_ATTR_NOT_FOUND,
        "HYPERSPACE State DB event attr not found" },
    { Error::HYPERSPACE_STATEDB_SESSION_EXISTS,
        "HYPERSPACE State DB session exists" },
    { Error::HYPERSPACE_STATEDB_SESSION_NOT_EXISTS,
        "HYPERSPACE State DB session does not exist" },
    { Error::HYPERSPACE_STATEDB_SESSION_ATTR_NOT_FOUND,
        "HYPERSPACE State DB session attr not found" },
    { Error::HYPERSPACE_STATEDB_HANDLE_EXISTS,
        "HYPERSPACE State DB handle exists" },
    { Error::HYPERSPACE_STATEDB_HANDLE_NOT_EXISTS,
        "HYPERSPACE State DB handle does not exist" },
    { Error::HYPERSPACE_STATEDB_HANDLE_ATTR_NOT_FOUND,
        "HYPERSPACE State DB handle attr not found" },
    { Error::HYPERSPACE_STATEDB_NODE_EXISTS,
        "HYPERSPACE State DB node exists" },
    { Error::HYPERSPACE_STATEDB_NODE_NOT_EXISTS,
        "HYPERSPACE State DB node does not exist" },
    { Error::HYPERSPACE_STATEDB_NODE_ATTR_NOT_FOUND,
        "HYPERSPACE State DB node attr not found" },
    { Error::HYPERSPACE_VERSION_MISMATCH,
        "HYPERSPACE client/server protocol version mismatch" },

    { Error::MASTER_TABLE_EXISTS,         "MASTER table exists" },
    { Error::MASTER_BAD_SCHEMA,           "MASTER bad schema" },
    { Error::MASTER_NOT_RUNNING,          "MASTER not running" },
    { Error::MASTER_NO_RANGESERVERS,      "MASTER no range servers" },
    { Error::MASTER_FILE_NOT_LOCKED,      "MASTER file not locked" },
    { Error::MASTER_RANGESERVER_ALREADY_REGISTERED,
        "MASTER range server with same location already registered" },
    { Error::MASTER_BAD_COLUMN_FAMILY,    "MASTER bad column family" },
    { Error::MASTER_SCHEMA_GENERATION_MISMATCH,
        "Master schema generation mismatch" },
    { Error::MASTER_LOCATION_ALREADY_ASSIGNED,
      "MASTER location already assigned" },
    { Error::MASTER_LOCATION_INVALID, "MASTER location invalid" },
    { Error::MASTER_OPERATION_IN_PROGRESS, "MASTER operation in progress" },

    { Error::RANGESERVER_GENERATION_MISMATCH,
        "RANGE SERVER generation mismatch" },
    { Error::RANGESERVER_RANGE_ALREADY_LOADED,
        "RANGE SERVER range already loaded" },
    { Error::RANGESERVER_RANGE_MISMATCH,       "RANGE SERVER range mismatch" },
    { Error::RANGESERVER_NONEXISTENT_RANGE,
        "RANGE SERVER non-existent range" },
    { Error::RANGESERVER_OUT_OF_RANGE,         "RANGE SERVER out of range" },
    { Error::RANGESERVER_RANGE_NOT_FOUND,      "RANGE SERVER range not found" },
    { Error::RANGESERVER_INVALID_SCANNER_ID,
        "RANGE SERVER invalid scanner id" },
    { Error::RANGESERVER_SCHEMA_PARSE_ERROR,
        "RANGE SERVER schema parse error" },
    { Error::RANGESERVER_SCHEMA_INVALID_CFID,
        "RANGE SERVER invalid column family id" },
    { Error::RANGESERVER_INVALID_COLUMNFAMILY,
        "RANGE SERVER invalid column family" },
    { Error::RANGESERVER_TRUNCATED_COMMIT_LOG,
        "RANGE SERVER truncated commit log" },
    { Error::RANGESERVER_NO_METADATA_FOR_RANGE,
        "RANGE SERVER no metadata for range" },
    { Error::RANGESERVER_SHUTTING_DOWN,        "RANGE SERVER shutting down" },
    { Error::RANGESERVER_CORRUPT_COMMIT_LOG,
        "RANGE SERVER corrupt commit log" },
    { Error::RANGESERVER_UNAVAILABLE,          "RANGE SERVER unavailable" },
    { Error::RANGESERVER_REVISION_ORDER_ERROR,
        "RANGE SERVER supplied revision is not strictly increasing" },
    { Error::RANGESERVER_ROW_OVERFLOW,         "RANGE SERVER row overflow" },
    { Error::RANGESERVER_TABLE_NOT_FOUND,      "RANGE SERVER table not found" },
    { Error::RANGESERVER_BAD_SCAN_SPEC,
        "RANGE SERVER bad scan specification" },
    { Error::RANGESERVER_CLOCK_SKEW,
        "RANGE SERVER clock skew detected" },
    { Error::RANGESERVER_BAD_CELLSTORE_FILENAME,
        "RANGE SERVER bad CellStore filename" },
    { Error::RANGESERVER_CORRUPT_CELLSTORE,
        "RANGE SERVER corrupt CellStore" },
    { Error::RANGESERVER_TABLE_DROPPED, "RANGE SERVER table dropped" },
    { Error::RANGESERVER_UNEXPECTED_TABLE_ID, "RANGE SERVER unexpected table ID" },
    { Error::RANGESERVER_RANGE_BUSY, "RANGE SERVER range busy" },
    { Error::RANGESERVER_BAD_CELL_INTERVAL, "RANGE SERVER bad cell interval" },
    { Error::RANGESERVER_SHORT_CELLSTORE_READ, "RANGE SERVER short cellstore read" },
    { Error::RANGESERVER_RANGE_NOT_ACTIVE, "RANGE SERVER range no longer active" },
    { Error::HQL_BAD_LOAD_FILE_FORMAT,         "HQL bad load file format" },
    { Error::METALOG_VERSION_MISMATCH, "METALOG version mismatch" },
    { Error::METALOG_BAD_RS_HEADER, "METALOG bad range server metalog header" },
    { Error::METALOG_BAD_HEADER,  "METALOG bad metalog header" },
    { Error::METALOG_ENTRY_TRUNCATED,   "METALOG entry truncated" },
    { Error::METALOG_CHECKSUM_MISMATCH, "METALOG checksum mismatch" },
    { Error::METALOG_ENTRY_BAD_TYPE, "METALOG bad entry type" },
    { Error::METALOG_ENTRY_BAD_ORDER, "METALOG entry out of order" },
    { Error::SERIALIZATION_INPUT_OVERRUN,
        "SERIALIZATION input buffer overrun" },
    { Error::SERIALIZATION_BAD_VINT,      "SERIALIZATION bad vint encoding" },
    { Error::SERIALIZATION_BAD_VSTR,      "SERIALIZATION bad vstr encoding" },
    { Error::SERIALIZATION_VERSION_MISMATCH, "SERIALIZATION version mismatch" },
    { Error::THRIFTBROKER_BAD_SCANNER_ID, "THRIFT BROKER bad scanner id" },
    { Error::THRIFTBROKER_BAD_MUTATOR_ID, "THRIFT BROKER bad mutator id" },
    { Error::THRIFTBROKER_BAD_NAMESPACE_ID, "THRIFT BROKER bad namespace id" },
    { Error::THRIFTBROKER_BAD_FUTURE_ID,    "THRIFT BROKER bad future id" },
    { 0, 0 }
  };

  typedef hash_map<int, const char *>  TextMap;

  TextMap &build_text_map() {
    TextMap *map = new TextMap();
    for (int i=0; error_info[i].text != 0; i++)
      (*map)[error_info[i].code] = error_info[i].text;
    return *map;
  }

  TextMap &text_map = build_text_map();

} // local namespace

const char *Error::get_text(int error) {
  const char *text = text_map[error];
  if (text == 0)
    return "ERROR NOT REGISTERED";
  return text;
}

void Error::generate_html_error_code_documentation(std::ostream &out) {
  out << "<table border=\"1\" cellpadding=\"4\" cellspacing=\"1\" style=\"width: 720px; \">\n";
  out << "<thead><tr><th scope=\"col\">Code<br />(hexidecimal)</th>\n";
  out << "<th scope=\"col\">Code<br />(decimal)</th>\n";
  out << "<th scope=\"col\">Description</th></tr></thead><tbody>\n";

  for (size_t i=0; error_info[i].text; i++) {
    if (error_info[i].code >= 0)
      out << "<tr><td style=\"text-align: right; \"><code>0x" << std::hex << error_info[i].code << "</code></td>\n";
    else
      out << "<tr><td style=\"text-align: right; \"><code></code></td>\n";
    out << "<td style=\"text-align: right; \"><code>" << std::dec << error_info[i].code << "</code></td>\n";
    out << "<td>" << error_info[i].text << "</td></tr>\n";
  }
  out << "</tbody></table>\n" << std::flush;
}

namespace Hypertable {

  const char *relative_fname(const Exception &e) {
    if (e.file()) {
      const char *ptr = strstr(e.file(), "src/cc/");
      return ptr ? ptr : e.file();
    }
    return "";
  }

std::ostream &operator<<(std::ostream &out, const Exception &e) {
  out <<"Hypertable::Exception: "<< e.message() <<" - "
      << Error::get_text(e.code());

  if (e.line()) {
    out <<"\n\tat "<< e.func() <<" (";
    if (Logger::show_line_numbers)
      out << e.file() <<':'<< e.line();
    else
      out << relative_fname(e);
    out <<')';
  }

  int prev_code = e.code();

  for (Exception *prev = e.prev; prev; prev = prev->prev) {
    out <<"\n\tat "<< (prev->func() ? prev->func() : "-") <<" (";
    if (Logger::show_line_numbers)
      out << (prev->file() ? prev->file() : "-") <<':'<< prev->line();
    else
      out << relative_fname(*prev);
    out <<"): " << prev->message();

    if (prev->code() != prev_code) {
      out <<" - "<< Error::get_text(prev->code());
      prev_code = prev->code();
    }
  }
  return out;
}

std::ostream &
Exception::render_messages(std::ostream &out, const char *sep) const {
  out << message() <<" - "<< Error::get_text(m_error);

  for (Exception *p = prev; p; p = p->prev)
    out << sep << p->message();

  return out;
}

} // namespace Hypertable
