/*
 * Copyright (C) 2007-2015 Hypertable, Inc.
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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

/// @file
/// ThriftBroker server.
/// This file contains the main function and server definition for the
/// ThriftBroker, a server that provides client language independent access to
/// Hypertable.

#include <Common/Compat.h>

#include <ThriftBroker/Config.h>
#include <ThriftBroker/MetricsHandler.h>
#include <ThriftBroker/SerializedCellsReader.h>
#include <ThriftBroker/SerializedCellsWriter.h>
#include <ThriftBroker/ThriftHelper.h>

#include <HyperAppHelper/Unique.h>
#include <HyperAppHelper/Error.h>

#include <Hypertable/Lib/Client.h>
#include <Hypertable/Lib/Future.h>
#include <Hypertable/Lib/HqlInterpreter.h>
#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/NamespaceListing.h>
#include <Hypertable/Lib/ProfileDataScanner.h>

#include <Common/Cronolog.h>
#include <Common/Init.h>
#include <Common/Logger.h>
#include <Common/Mutex.h>
#include <Common/Random.h>
#include <Common/System.h>
#include <Common/Time.h>

#include <concurrency/ThreadManager.h>
#include <protocol/TBinaryProtocol.h>
#include <server/TThreadedServer.h>
#include <transport/TBufferTransports.h>
#include <transport/TServerSocket.h>
#include <transport/TSocket.h>
#include <transport/TTransportUtils.h>

#include <boost/shared_ptr.hpp>

#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

extern "C" {
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
}

/// @defgroup ThriftBroker ThriftBroker
/// %Thrift broker.
/// The @ref ThriftBroker module contains the definition of the ThriftBroker.
/// @{

namespace {
  /// Smart pointer to metrics gathering handler
  MetricsHandlerPtr g_metrics_handler;
  /// Enable slow query log
  bool g_log_slow_queries {};
  /// Slow query latency threshold
  int32_t g_slow_query_latency_threshold {};
  /// Slow query log
  Cronolog *g_slow_query_log {};
}

#define THROW_TE(_code_, _str_) do { ThriftGen::ClientException te; \
  te.code = _code_; \
  te.message.append(Error::get_text(_code_)); \
  te.message.append(" - "); \
  te.message.append(_str_); \
  te.__isset.code = te.__isset.message = true; \
  throw te; \
} while (0)

#define RETHROW(_expr_) catch (Hypertable::Exception &e) { \
  std::ostringstream oss;  oss << HT_FUNC << " " << _expr_ << ": "<< e; \
  HT_ERROR_OUT << oss.str() << HT_END; \
  oss.str(""); \
  oss << _expr_; \
  g_metrics_handler->error_increment(); \
  THROW_TE(e.code(), oss.str()); \
}

#define LOG_API_START(_expr_) \
  boost::xtime start_time, end_time; \
  std::ostringstream logging_stream;\
  ScannerInfoPtr scanner_info;\
  g_metrics_handler->request_increment(); \
  if (m_context.log_api || g_log_slow_queries) {\
    boost::xtime_get(&start_time, TIME_UTC_);\
    if (m_context.log_api)\
      logging_stream << "API " << __func__ << ": " << _expr_;\
  }

#define LOG_API_FINISH \
  if (m_context.log_api || (g_log_slow_queries && scanner_info)) { \
    boost::xtime_get(&end_time, TIME_UTC_); \
    if (m_context.log_api)\
      std::cout << start_time.sec <<'.'<< std::setw(9) << std::setfill('0') << start_time.nsec <<" API "<< __func__ <<": "<< logging_stream.str() << " latency=" << xtime_diff_millis(start_time, end_time) << std::endl; \
    if (scanner_info) \
      scanner_info->latency += xtime_diff_millis(start_time, end_time);\
  }

#define LOG_API_FINISH_E(_expr_) \
  if (m_context.log_api || (g_log_slow_queries && scanner_info)) { \
    boost::xtime_get(&end_time, TIME_UTC_); \
    if (m_context.log_api)\
      std::cout << start_time.sec <<'.'<< std::setw(9) << std::setfill('0') << start_time.nsec <<" API "<< __func__ <<": "<< logging_stream.str() << _expr_ << " latency=" << xtime_diff_millis(start_time, end_time) << std::endl; \
    if (scanner_info)\
      scanner_info->latency += xtime_diff_millis(start_time, end_time);\
  }


#define LOG_API(_expr_) do { \
  if (m_context.log_api) \
    std::cout << hires_ts <<" API "<< __func__ <<": "<< _expr_ << std::endl; \
} while (0)

#define LOG_HQL_RESULT(_res_) do { \
  if (m_context.log_api) \
    cout << hires_ts <<" API "<< __func__ <<": result: "; \
  if (Logger::logger->isDebugEnabled()) \
    cout << _res_; \
  else if (m_context.log_api) { \
    if (_res_.__isset.results) \
      cout <<"results.size=" << _res_.results.size(); \
    if (_res_.__isset.cells) \
      cout <<"cells.size=" << _res_.cells.size(); \
    if (_res_.__isset.scanner) \
      cout <<"scanner="<< _res_.scanner; \
    if (_res_.__isset.mutator) \
      cout <<"mutator="<< _res_.mutator; \
  } \
  cout << std::endl; \
} while(0)

#define LOG_SLOW_QUERY(_pd_, _ns_, _hql_) do {   \
  if (g_log_slow_queries) { \
    boost::xtime_get(&end_time, TIME_UTC_); \
    int64_t latency_ms = xtime_diff_millis(start_time, end_time); \
    if (latency_ms >= g_slow_query_latency_threshold) \
      log_slow_query(__func__, start_time, end_time, latency_ms, _pd_, _ns_, _hql_); \
  } \
} while(0)

#define LOG_SLOW_QUERY_SCANNER(_scanner_, _ns_, _table_, _ss_) do {      \
  if (g_log_slow_queries) { \
    ProfileDataScanner pd; \
    _scanner_->get_profile_data(pd); \
    boost::xtime_get(&end_time, TIME_UTC_); \
    int64_t latency_ms = xtime_diff_millis(start_time, end_time); \
    if (latency_ms >= g_slow_query_latency_threshold) \
      log_slow_query_scanspec(__func__, start_time, end_time, latency_ms, pd, _ns_, _table_, _ss_); \
  } \
} while(0)

namespace Hypertable { namespace ThriftBroker {

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;
using namespace apache::thrift::concurrency;

using namespace Config;
using namespace ThriftGen;
using namespace boost;
using namespace std;


class SharedMutatorMapKey {
public:
  SharedMutatorMapKey(Hypertable::Namespace *ns, const String &tablename,
      const ThriftGen::MutateSpec &mutate_spec)
    : m_namespace(ns), m_tablename(tablename), m_mutate_spec(mutate_spec) {}

  int compare(const SharedMutatorMapKey &skey) const {
    int64_t  cmp;

    cmp = (int64_t)m_namespace - (int64_t)skey.m_namespace;
    if (cmp != 0)
      return cmp;
    cmp = m_tablename.compare(skey.m_tablename);
    if (cmp != 0)
      return cmp;
    cmp = m_mutate_spec.appname.compare(skey.m_mutate_spec.appname);
    if (cmp != 0)
      return cmp;
    cmp = m_mutate_spec.flush_interval - skey.m_mutate_spec.flush_interval;
    if (cmp != 0)
      return cmp;
    cmp = m_mutate_spec.flags - skey.m_mutate_spec.flags;
    return cmp;
  }

  Hypertable::Namespace *m_namespace;
  String m_tablename;
  ThriftGen::MutateSpec m_mutate_spec;
};

inline bool operator < (const SharedMutatorMapKey &skey1,
        const SharedMutatorMapKey &skey2) {
  return skey1.compare(skey2) < 0;
}

typedef Meta::list<ThriftBrokerPolicy, DefaultCommPolicy> Policies;

typedef std::map<SharedMutatorMapKey, TableMutator * > SharedMutatorMap;
typedef std::unordered_map< ::int64_t, ClientObjectPtr> ObjectMap;
typedef std::vector<ThriftGen::Cell> ThriftCells;
typedef std::vector<CellAsArray> ThriftCellsAsArrays;

class Context {
public:
  Context() {
    client = new Hypertable::Client();
    log_api = Config::get_bool("ThriftBroker.API.Logging");
    next_threshold = Config::get_i32("ThriftBroker.NextThreshold");
    future_capacity = Config::get_i32("ThriftBroker.Future.Capacity");
  }
  Hypertable::Client *client;
  Mutex shared_mutator_mutex;
  SharedMutatorMap shared_mutator_map;
  bool log_api;
  ::uint32_t next_threshold;
  ::uint32_t future_capacity;
};

int64_t
cell_str_to_num(const std::string &from, const char *label,
        int64_t min_num = INT64_MIN, int64_t max_num = INT64_MAX) {
  char *endp;

  int64_t value = strtoll(from.data(), &endp, 0);

  if (endp - from.data() != (int)from.size()
      || value < min_num || value > max_num)
    HT_THROWF(Error::BAD_KEY, "Error converting %s to %s", from.c_str(), label);

  return value;
}

void
convert_scan_spec(const ThriftGen::ScanSpec &tss, Hypertable::ScanSpec &hss) {
  if (tss.__isset.row_limit)
    hss.row_limit = tss.row_limit;

  if (tss.__isset.cell_limit)
    hss.cell_limit = tss.cell_limit;

  if (tss.__isset.cell_limit_per_family)
    hss.cell_limit_per_family = tss.cell_limit_per_family;

  if (tss.__isset.versions)
    hss.max_versions = tss.versions;

  if (tss.__isset.start_time)
    hss.time_interval.first = tss.start_time;

  if (tss.__isset.end_time)
    hss.time_interval.second = tss.end_time;

  if (tss.__isset.return_deletes)
    hss.return_deletes = tss.return_deletes;

  if (tss.__isset.keys_only)
    hss.keys_only = tss.keys_only;

  if (tss.__isset.row_regexp)
    hss.row_regexp = tss.row_regexp.c_str();

  if (tss.__isset.value_regexp)
    hss.value_regexp = tss.value_regexp.c_str();

  if (tss.__isset.scan_and_filter_rows)
    hss.scan_and_filter_rows = tss.scan_and_filter_rows;

  if (tss.__isset.do_not_cache)
    hss.do_not_cache = tss.do_not_cache;

  if (tss.__isset.row_offset)
    hss.row_offset = tss.row_offset;

  if (tss.__isset.cell_offset)
    hss.cell_offset = tss.cell_offset;

  if (tss.__isset.and_column_predicates)
    hss.and_column_predicates = tss.and_column_predicates;

  // shallow copy
  const char *start_row;
  const char *end_row;
  foreach_ht(const ThriftGen::RowInterval &ri, tss.row_intervals) {
    start_row = ri.__isset.start_row ?
                ri.start_row.c_str() :
                ri.__isset.start_row_binary ?
                ri.start_row_binary.c_str() :
                "";
    end_row = ri.__isset.end_row ?
              ri.end_row.c_str() :
              ri.__isset.end_row_binary ?
              ri.end_row_binary.c_str() :
              Hypertable::Key::END_ROW_MARKER;
    hss.row_intervals.push_back(Hypertable::RowInterval(
      start_row, ri.__isset.start_inclusive && ri.start_inclusive,
      end_row, ri.__isset.end_inclusive && ri.end_inclusive));
  }

  foreach_ht(const ThriftGen::CellInterval &ci, tss.cell_intervals)
    hss.cell_intervals.push_back(Hypertable::CellInterval(
        ci.__isset.start_row ? ci.start_row.c_str() : "",
        ci.start_column.c_str(),
        ci.__isset.start_inclusive && ci.start_inclusive,
        ci.__isset.end_row ? ci.end_row.c_str() : Hypertable::Key::END_ROW_MARKER,
        ci.end_column.c_str(),
        ci.__isset.end_inclusive && ci.end_inclusive));

  foreach_ht(const std::string &col, tss.columns)
    hss.columns.push_back(col.c_str());

  foreach_ht(const ThriftGen::ColumnPredicate &cp, tss.column_predicates) {
    HT_INFOF("%s:%s %s", cp.column_family.c_str(), cp.column_qualifier.c_str(),
             cp.__isset.value ? cp.value.c_str() : "");
    hss.column_predicates.push_back(
      Hypertable::ColumnPredicate(
        cp.__isset.column_family ? cp.column_family.c_str() : 0,
        cp.__isset.column_qualifier ? cp.column_qualifier.c_str() : 0,
        cp.operation,
        cp.__isset.value ? cp.value.c_str() : 0,
        cp.__isset.value ? cp.value.size() : 0));
  }
}


void
convert_scan_spec(const ThriftGen::ScanSpec &tss, Hypertable::ScanSpecBuilder &ssb) {
  if (tss.__isset.row_limit)
    ssb.set_row_limit(tss.row_limit);

  if (tss.__isset.cell_limit)
    ssb.set_cell_limit(tss.cell_limit);

  if (tss.__isset.cell_limit_per_family)
    ssb.set_cell_limit_per_family(tss.cell_limit_per_family);

  if (tss.__isset.versions)
    ssb.set_max_versions(tss.versions);

  if (tss.__isset.start_time)
    ssb.set_start_time(tss.start_time);

  if (tss.__isset.end_time)
    ssb.set_end_time(tss.end_time);

  if (tss.__isset.return_deletes)
    ssb.set_return_deletes(tss.return_deletes);

  if (tss.__isset.keys_only)
    ssb.set_keys_only(tss.keys_only);

  if (tss.__isset.row_regexp)
    ssb.set_row_regexp(tss.row_regexp.c_str());

  if (tss.__isset.value_regexp)
    ssb.set_value_regexp(tss.value_regexp.c_str());

  if (tss.__isset.scan_and_filter_rows)
    ssb.set_scan_and_filter_rows(tss.scan_and_filter_rows);

  if (tss.__isset.do_not_cache)
    ssb.set_do_not_cache(tss.do_not_cache);

  if (tss.__isset.row_offset)
    ssb.set_row_offset(tss.row_offset);

  if (tss.__isset.cell_offset)
    ssb.set_cell_offset(tss.cell_offset);

  if (tss.__isset.and_column_predicates)
    ssb.set_and_column_predicates(tss.and_column_predicates);

  // columns
  ssb.reserve_columns(tss.columns.size());
  for (auto & col : tss.columns)
    ssb.add_column(col);

  // row intervals
  const char *start_row;
  const char *end_row;
  ssb.reserve_rows(tss.row_intervals.size());
  for (auto & ri : tss.row_intervals) {
    start_row = ri.__isset.start_row ?
                ri.start_row.c_str() :
                ri.__isset.start_row_binary ?
                ri.start_row_binary.c_str() : "";
    end_row = ri.__isset.end_row ?
              ri.end_row.c_str() :
              ri.__isset.end_row_binary ?
              ri.end_row_binary.c_str() : Hypertable::Key::END_ROW_MARKER;
    ssb.add_row_interval(
      start_row, ri.__isset.start_inclusive && ri.start_inclusive,
      end_row, ri.__isset.end_inclusive && ri.end_inclusive);
  }

  // cell intervals
  ssb.reserve_cells(tss.cell_intervals.size());
  for (auto & ci : tss.cell_intervals)
    ssb.add_cell_interval(
        ci.__isset.start_row ? ci.start_row.c_str() : "",
        ci.start_column.c_str(),
        ci.__isset.start_inclusive && ci.start_inclusive,
        ci.__isset.end_row ? ci.end_row.c_str() : Hypertable::Key::END_ROW_MARKER,
        ci.end_column.c_str(),
        ci.__isset.end_inclusive && ci.end_inclusive);

  // column predicates
  ssb.reserve_column_predicates(tss.column_predicates.size());
  for (auto & cp : tss.column_predicates)
    ssb.add_column_predicate(
        cp.__isset.column_family ? cp.column_family.c_str() : 0,
        cp.__isset.column_qualifier ? cp.column_qualifier.c_str() : 0,
        cp.operation,
        cp.__isset.value ? cp.value.c_str() : 0,
        cp.__isset.value ? cp.value.size() : 0);
}

void
convert_cell(const ThriftGen::Cell &tcell, Hypertable::Cell &hcell) {
  // shallow copy
  if (tcell.key.__isset.row)
    hcell.row_key = tcell.key.row.c_str();

  if (tcell.key.__isset.column_family)
    hcell.column_family = tcell.key.column_family.c_str();

  if (tcell.key.__isset.column_qualifier)
    hcell.column_qualifier = tcell.key.column_qualifier.c_str();

  if (tcell.key.__isset.timestamp)
    hcell.timestamp = tcell.key.timestamp;

  if (tcell.key.__isset.revision)
    hcell.revision = tcell.key.revision;

  if (tcell.__isset.value) {
    hcell.value = (::uint8_t *)tcell.value.c_str();
    hcell.value_len = tcell.value.length();
  }

  if (tcell.key.__isset.flag)
    hcell.flag = tcell.key.flag;
}

void
convert_key(const ThriftGen::Key &tkey, Hypertable::KeySpec &hkey) {
  // shallow copy
  if (tkey.__isset.row) {
    hkey.row = tkey.row.c_str();
    hkey.row_len = tkey.row.size();
  }

  if (tkey.__isset.column_family)
    hkey.column_family = tkey.column_family.c_str();

  if (tkey.__isset.column_qualifier)
    hkey.column_qualifier = tkey.column_qualifier.c_str();

  if (tkey.__isset.timestamp)
    hkey.timestamp = tkey.timestamp;

  if (tkey.__isset.revision)
    hkey.revision = tkey.revision;
}

int32_t
convert_cell(const Hypertable::Cell &hcell, ThriftGen::Cell &tcell) {
  int32_t amount = sizeof(ThriftGen::Cell);

  tcell.key.row = hcell.row_key;
  amount += tcell.key.row.length();
  tcell.key.column_family = hcell.column_family;
  amount += tcell.key.column_family.length();

  if (hcell.column_qualifier && *hcell.column_qualifier) {
    tcell.key.column_qualifier = hcell.column_qualifier;
    tcell.key.__isset.column_qualifier = true;
    amount += tcell.key.column_qualifier.length();
  }
  else {
    tcell.key.column_qualifier = "";
    tcell.key.__isset.column_qualifier = false;
  }

  tcell.key.timestamp = hcell.timestamp;
  tcell.key.revision = hcell.revision;

  if (hcell.value && hcell.value_len) {
    tcell.value = std::string((char *)hcell.value, hcell.value_len);
    tcell.__isset.value = true;
    amount += hcell.value_len;
  }
  else {
    tcell.value = "";
    tcell.__isset.value = false;
  }

  tcell.key.flag = (KeyFlag::type)hcell.flag;
  tcell.key.__isset.row = tcell.key.__isset.column_family
      = tcell.key.__isset.timestamp = tcell.key.__isset.revision
      = tcell.key.__isset.flag = true;
  return amount;
}

void convert_cell(const CellAsArray &tcell, Hypertable::Cell &hcell) {
  int len = tcell.size();

  switch (len) {
  case 7: hcell.flag = cell_str_to_num(tcell[6], "cell flag", 0, 255);
  case 6: hcell.revision = cell_str_to_num(tcell[5], "revision");
  case 5: hcell.timestamp = cell_str_to_num(tcell[4], "timestamp");
  case 4: hcell.value = (::uint8_t *)tcell[3].c_str();
          hcell.value_len = tcell[3].length();
  case 3: hcell.column_qualifier = tcell[2].c_str();
  case 2: hcell.column_family = tcell[1].c_str();
  case 1: hcell.row_key = tcell[0].c_str();
    break;
  default:
    HT_THROWF(Error::BAD_KEY, "CellAsArray: bad size: %d", len);
  }
}

int32_t convert_cell(const Hypertable::Cell &hcell, CellAsArray &tcell) {
  int32_t amount = 5*sizeof(std::string);
  tcell.resize(5);
  tcell[0] = hcell.row_key;
  amount += tcell[0].length();
  tcell[1] = hcell.column_family;
  amount += tcell[1].length();
  tcell[2] = hcell.column_qualifier ? hcell.column_qualifier : "";
  amount += tcell[2].length();
  tcell[3] = std::string((char *)hcell.value, hcell.value_len);
  amount += tcell[3].length();
  tcell[4] = format("%llu", (Llu)hcell.timestamp);
  amount += tcell[4].length();
  return amount;
}

int32_t convert_cells(const Hypertable::Cells &hcells, ThriftCells &tcells) {
  // deep copy
  int32_t amount = sizeof(ThriftCells);
  tcells.resize(hcells.size());
  for(size_t ii=0; ii<hcells.size(); ++ii)
    amount += convert_cell(hcells[ii], tcells[ii]);

  return amount;
}

void convert_cells(const ThriftCells &tcells, Hypertable::Cells &hcells) {
  // shallow copy
  foreach_ht(const ThriftGen::Cell &tcell, tcells) {
    Hypertable::Cell hcell;
    convert_cell(tcell, hcell);
    hcells.push_back(hcell);
  }
}

int32_t convert_cells(const Hypertable::Cells &hcells, ThriftCellsAsArrays &tcells) {
  // deep copy
  int32_t amount = sizeof(CellAsArray);
  tcells.resize(hcells.size());
  for(size_t ii=0; ii<hcells.size(); ++ii) {
    amount += convert_cell(hcells[ii], tcells[ii]);
  }
 return amount;
}

int32_t convert_cells(Hypertable::Cells &hcells, CellsSerialized &tcells) {
  // deep copy
  int32_t amount = 0 ;
  for(size_t ii=0; ii<hcells.size(); ++ii) {
    amount += strlen(hcells[ii].row_key) + strlen(hcells[ii].column_family)
        + strlen(hcells[ii].column_qualifier) + 8 + 8 + 4 + 1
        + hcells[ii].value_len + 4;
  }
  SerializedCellsWriter writer(amount, true);
  for (size_t ii = 0; ii < hcells.size(); ++ii) {
    writer.add(hcells[ii]);
  }
  writer.finalize(SerializedCellsFlag::EOS);
  tcells = String((char *)writer.get_buffer(), writer.get_buffer_length());
  amount = tcells.size();
  return amount;
}

void
convert_cells(const ThriftCellsAsArrays &tcells, Hypertable::Cells &hcells) {
  // shallow copy
  foreach_ht(const CellAsArray &tcell, tcells) {
    Hypertable::Cell hcell;
    convert_cell(tcell, hcell);
    hcells.push_back(hcell);
  }
}

void convert_table_split(const Hypertable::TableSplit &hsplit,
        ThriftGen::TableSplit &tsplit) {
  /** start_row **/
  if (hsplit.start_row) {
    tsplit.start_row = hsplit.start_row;
    tsplit.__isset.start_row = true;
  }
  else {
    tsplit.start_row = "";
    tsplit.__isset.start_row = false;
  }

  /** end_row **/
  if (hsplit.end_row &&
      !(hsplit.end_row[0] == (char)0xff && hsplit.end_row[1] == (char)0xff)) {
    tsplit.end_row = hsplit.end_row;
    tsplit.__isset.end_row = true;
  }
  else {
    tsplit.end_row = Hypertable::Key::END_ROW_MARKER;
    tsplit.__isset.end_row = false;
  }

  /** location **/
  if (hsplit.location) {
    tsplit.location = hsplit.location;
    tsplit.__isset.location = true;
  }
  else {
    tsplit.location = "";
    tsplit.__isset.location = false;
  }

  /** ip_address **/
  if (hsplit.ip_address) {
    tsplit.ip_address = hsplit.ip_address;
    tsplit.__isset.ip_address = true;
  }
  else {
    tsplit.ip_address = "";
    tsplit.__isset.ip_address = false;
  }

  /** hostname **/
  if (hsplit.hostname) {
    tsplit.hostname = hsplit.hostname;
    tsplit.__isset.hostname = true;
  }
  else {
    tsplit.hostname = "";
    tsplit.__isset.hostname = false;
  }

}

bool convert_column_family_options(const Hypertable::ColumnFamilyOptions &hoptions,
                                   ThriftGen::ColumnFamilyOptions &toptions) {
  bool ret = false;
  if (hoptions.is_set_max_versions()) {
    toptions.__set_max_versions(hoptions.get_max_versions());
    ret = true;
  }
  if (hoptions.is_set_ttl()) {
    toptions.__set_ttl(hoptions.get_ttl());
    ret = true;
  }
  if (hoptions.is_set_time_order_desc()) {
    toptions.__set_time_order_desc(hoptions.get_time_order_desc());
    ret = true;
  }
  if (hoptions.is_set_counter()) {
    toptions.__set_counter(hoptions.get_counter());
    ret = true;
  }
  return ret;
}

void convert_column_family_options(const ThriftGen::ColumnFamilyOptions &toptions,
                                   Hypertable::ColumnFamilyOptions &hoptions) {
  if (toptions.__isset.max_versions)
    hoptions.set_max_versions(toptions.max_versions);
  if (toptions.__isset.ttl)
    hoptions.set_ttl(toptions.ttl);
  if (toptions.__isset.time_order_desc)
    hoptions.set_time_order_desc(toptions.time_order_desc);
  if (toptions.__isset.counter)
    hoptions.set_counter(toptions.counter);
}

bool convert_access_group_options(const Hypertable::AccessGroupOptions &hoptions,
                                  ThriftGen::AccessGroupOptions &toptions) {
  bool ret = false;
  if (hoptions.is_set_in_memory()) {
    toptions.__set_in_memory(hoptions.get_in_memory());
    ret = true;
  }
  if (hoptions.is_set_replication()) {
    toptions.__set_replication(hoptions.get_replication());
    ret = true;
  }
  if (hoptions.is_set_blocksize()) {
    toptions.__set_blocksize(hoptions.get_blocksize());
    ret = true;
  }
  if (hoptions.is_set_compressor()) {
    toptions.__set_compressor(hoptions.get_compressor());
    ret = true;
  }
  if (hoptions.is_set_bloom_filter()) {
    toptions.__set_bloom_filter(hoptions.get_bloom_filter());
    ret = true;
  }
  return ret;
}

void convert_access_group_options(const ThriftGen::AccessGroupOptions &toptions,
                                  Hypertable::AccessGroupOptions &hoptions) {
  if (toptions.__isset.in_memory)
    hoptions.set_in_memory(toptions.in_memory);
  if (toptions.__isset.replication)
    hoptions.set_replication(toptions.replication);
  if (toptions.__isset.blocksize)
    hoptions.set_blocksize(toptions.blocksize);
  if (toptions.__isset.compressor)
    hoptions.set_compressor(toptions.compressor);
  if (toptions.__isset.bloom_filter)
    hoptions.set_bloom_filter(toptions.bloom_filter);
}


void convert_schema(const Hypertable::SchemaPtr &hschema,
                    ThriftGen::Schema &tschema) {

  if (hschema->get_generation())
    tschema.__set_generation(hschema->get_generation());

  tschema.__set_version(hschema->get_version());
  
  if (hschema->get_group_commit_interval())
    tschema.__set_group_commit_interval(hschema->get_group_commit_interval());

  for (auto ag_spec : hschema->get_access_groups()) {
    ThriftGen::AccessGroupSpec tag;
    tag.name = ag_spec->get_name();
    tag.__set_generation(ag_spec->get_generation());
    if (convert_access_group_options(ag_spec->options(), tag.options))
      tag.__isset.options = true;      
    if (convert_column_family_options(ag_spec->defaults(), tag.defaults))
      tag.__isset.defaults = true;
    tschema.access_groups[ag_spec->get_name()] = tag;
    tschema.__isset.access_groups = true;
  }

  for (auto cf_spec : hschema->get_column_families()) {
    ThriftGen::ColumnFamilySpec tcf;
    tcf.name = cf_spec->get_name();
    tcf.access_group = cf_spec->get_access_group();
    tcf.deleted = cf_spec->get_deleted();
    if (cf_spec->get_generation())
      tcf.__set_generation(cf_spec->get_generation());
    if (cf_spec->get_id())
      tcf.__set_id(cf_spec->get_id());
    tcf.value_index = cf_spec->get_value_index();
    tcf.qualifier_index = cf_spec->get_qualifier_index();
    if (convert_column_family_options(cf_spec->options(), tcf.options))
      tcf.__isset.options = true;
    tschema.column_families[cf_spec->get_name()] = tcf;
    tschema.__isset.column_families = true;
  }

  if (convert_access_group_options(hschema->access_group_defaults(),
                                   tschema.access_group_defaults))
      tschema.__isset.access_group_defaults = true;

  if (convert_column_family_options(hschema->column_family_defaults(),
                                    tschema.column_family_defaults))
      tschema.__isset.column_family_defaults = true;

}

void convert_schema(const ThriftGen::Schema &tschema,
                    Hypertable::SchemaPtr &hschema) {

  if (tschema.__isset.generation)
    hschema->set_generation(tschema.generation);

  hschema->set_version(tschema.version);
  
  hschema->set_group_commit_interval(tschema.group_commit_interval);

  convert_access_group_options(tschema.access_group_defaults,
                               hschema->access_group_defaults());

  convert_column_family_options(tschema.column_family_defaults,
                                hschema->column_family_defaults());

  bool need_default = true;
  unordered_map<string, Hypertable::AccessGroupSpec *> ag_map;
  for (auto & entry : tschema.access_groups) {
    if (entry.second.name == "default")
      need_default = false;
    Hypertable::AccessGroupSpec *ag = new Hypertable::AccessGroupSpec(entry.second.name);
    if (entry.second.__isset.generation)
      ag->set_generation(entry.second.generation);
    ag_map[entry.second.name] = ag;
    Hypertable::AccessGroupOptions ag_options;
    convert_access_group_options(entry.second.options, ag_options);
    ag->merge_options(ag_options);
    Hypertable::ColumnFamilyOptions cf_defaults;
    convert_column_family_options(entry.second.defaults, cf_defaults);
    ag->merge_defaults(cf_defaults);
  }
  if (need_default)
    ag_map["default"] = new Hypertable::AccessGroupSpec("default");

  for (auto & entry : tschema.column_families) {
    Hypertable::ColumnFamilySpec *cf = new Hypertable::ColumnFamilySpec();
    cf->set_name(entry.second.name);
    if (entry.second.access_group.empty())
      cf->set_access_group("default");
    else
      cf->set_access_group(entry.second.access_group);
    cf->set_deleted(entry.second.deleted);
    if (entry.second.__isset.generation)
      cf->set_generation(entry.second.generation);
    if (entry.second.__isset.id)
      cf->set_id(entry.second.id);
    cf->set_value_index(entry.second.value_index);
    cf->set_qualifier_index(entry.second.qualifier_index);
    Hypertable::ColumnFamilyOptions cf_options;
    convert_column_family_options(entry.second.options, cf_options);
    cf->merge_options(cf_options);
    // Add column family to corresponding schema
    auto iter = ag_map.find(cf->get_access_group());
    if (iter == ag_map.end())
      HT_THROWF(Error::BAD_SCHEMA,
                "Undefined access group '%s' referenced by column '%s'",
                cf->get_access_group().c_str(), cf->get_name().c_str());
    iter->second->add_column(cf);
  }

  // Add access groups to schema
  for (auto & entry : ag_map)
    hschema->add_access_group(entry.second);

}

class ScannerInfo {
public:
  ScannerInfo(int64_t ns) : ns(ns), scan_spec_builder(128) { }
  ScannerInfo(int64_t ns, const string &t) :
    ns(ns), table(t), scan_spec_builder(128) { }
  int64_t ns;
  string hql;
  const string table;
  ScanSpecBuilder scan_spec_builder;
  int64_t latency {};
};
typedef std::shared_ptr<ScannerInfo> ScannerInfoPtr;

class ServerHandler;

template <class ResultT, class CellT>
struct HqlCallback : HqlInterpreter::Callback {
  typedef HqlInterpreter::Callback Parent;

  ResultT &result;
  ServerHandler &handler;
  ProfileDataScanner profile_data;
  int64_t ns {};
  string hql;
  bool flush, buffered;
  bool is_scan {};

  HqlCallback(ResultT &r, ServerHandler *handler, const ThriftGen::Namespace ns,
              const String &hql, bool flush, bool buffered)
    : result(r), handler(*handler), ns(ns), hql(hql), flush(flush),
      buffered(buffered) { }

  virtual void on_return(const String &);
  virtual void on_scan(TableScanner &);
  virtual void on_finish(TableMutator *);

};


class ServerHandler : public HqlServiceIf {
  struct Statistics {
    Statistics()
      : scanners(0), async_scanners(0), mutators(0), async_mutators(0),
        shared_mutators(0), namespaces(0), futures(0) {
    }

    bool operator==(const Statistics &other) {
      return scanners == other.scanners
          && async_scanners == other.async_scanners
          && mutators == other.mutators
          && async_mutators == other.async_mutators
          && shared_mutators == other.shared_mutators
          && namespaces == other.namespaces
          && futures == other.futures;
    }

    void operator=(const Statistics &other) {
      scanners = other.scanners;
      async_scanners = other.async_scanners;
      mutators = other.mutators;
      async_mutators = other.async_mutators;
      shared_mutators = other.shared_mutators;
      namespaces = other.namespaces;
      futures = other.futures;
    }

    int scanners;
    int async_scanners;
    int mutators;
    int async_mutators;
    int shared_mutators;
    int namespaces;
    int futures;
  };

public:

  ServerHandler(const String& remote_peer, Context &c)
    : m_remote_peer(remote_peer), m_context(c) {
  }

  virtual ~ServerHandler() {
    ScopedLock lock(m_mutex);
    if (!m_object_map.empty())
      HT_WARNF("Destroying ServerHandler for remote peer %s with %d objects in map",
               m_remote_peer.c_str(),
               (int)m_object_map.size());
    // Clear reference map.  Force each object to be destroyed individually and
    // catch and log exceptions.
    for (auto entry : m_reference_map) {
      try {
        entry.second = nullptr;
      }
      catch (Exception &e) {
        HT_ERROR_OUT << e << HT_END;
      }
    }
    try {
      m_reference_map.clear();
    }
    catch (Exception &e) {
    }
    // Clear object map.  Force each object to be destroyed individually and
    // catch and log exceptions.
    for (auto entry : m_object_map) {
      try {
        entry.second = nullptr;
      }
      catch (Exception &e) {
        HT_ERROR_OUT << e << HT_END;
      }
    }
    try {
      m_object_map.clear();
    }
    catch (Exception &e) {
    }
    // Clear cached object map.  Force each object to be destroyed individually
    // and catch and log exceptions.
    for (auto entry : m_cached_object_map) {
      try {
        entry.second = nullptr;
      }
      catch (Exception &e) {
        HT_ERROR_OUT << e << HT_END;
      }
    }
    try {
      m_cached_object_map.clear();
    }
    catch (Exception &e) {
    }
  }

  const String& remote_peer() const {
    return m_remote_peer;
  }

  void log_slow_query(const char *func_name, boost::xtime start_time,
                      boost::xtime end_time, int64_t latency_ms,
                      ProfileDataScanner &profile_data,
                      Hypertable::Namespace *ns, const string &hql) {

    // Build servers string
    string servers;
    bool first = true;
    servers.reserve(profile_data.servers.size()*6);
    for (auto & server : profile_data.servers) {
      if (first)
        first = false;
      else
        servers.append(",");
      servers.append(server);
    }

    string ns_str(ns->get_name());
    if (ns_str.empty())
      ns_str.append("/");
    else if (ns_str[0] != '/')
      ns_str = string("/") + ns_str;

    // Strip HQL string of newlines
    const char *hql_ptr = hql.c_str();
    string hql_cleaned;
    if (hql.find_first_of('\n') != string::npos) {
      hql_cleaned.reserve(hql.length());
      const char *base = hql.c_str();
      const char *ptr = base;
      while (*ptr) {
        while (*ptr && *ptr != '\n')
          ptr++;
        hql_cleaned.append(base, ptr-base);
        if (*ptr) {
          hql_cleaned.append(" ", 1);
          base = ++ptr;
        }
      }
      hql_ptr = hql_cleaned.c_str();
    }

    string line = format("%lld %s %s %lld %d %d %lld %lld %lld %s %s %s",
                         (Lld)start_time.sec, func_name, m_remote_peer.c_str(),
                         (Lld)latency_ms, profile_data.subscanners,
                         profile_data.scanblocks,
                         (Lld)profile_data.bytes_returned,
                         (Lld)profile_data.bytes_scanned,
                         (Lld)profile_data.disk_read, servers.c_str(),
                         ns_str.c_str(), hql_ptr);
    g_slow_query_log->write((time_t)end_time.sec, line);

  }

  void log_slow_query_scanspec(const char *func_name, boost::xtime start_time,
                               boost::xtime end_time, int64_t latency_ms,
                               ProfileDataScanner &profile_data,
                               Hypertable::Namespace *ns, const string &table,
                               Hypertable::ScanSpec &ss) {

    // Build servers string
    string servers;
    bool first = true;
    servers.reserve(profile_data.servers.size()*6);
    for (auto & server : profile_data.servers) {
      if (first)
        first = false;
      else
        servers.append(",");
      servers.append(server);
    }

    string ns_str(ns->get_name());
    if (ns_str.empty())
      ns_str.append("/");
    else if (ns_str[0] != '/')
      ns_str = string("/") + ns_str;

    string line = format("%lld %s %s %lld %d %d %lld %lld %lld %s %s %s",
                         (Lld)start_time.sec, func_name, m_remote_peer.c_str(),
                         (Lld)latency_ms, profile_data.subscanners,
                         profile_data.scanblocks,
                         (Lld)profile_data.bytes_returned,
                         (Lld)profile_data.bytes_scanned,
                         (Lld)profile_data.disk_read, servers.c_str(),
                         ns_str.c_str(), ss.render_hql(table).c_str());
    g_slow_query_log->write((time_t)end_time.sec, line);
  }
  

  virtual void
  hql_exec(HqlResult& result, const ThriftGen::Namespace ns,
          const String &hql, bool noflush, bool unbuffered) {
    LOG_API_START("namespace=" << ns << " hql=" << hql << " noflush=" << noflush
            << " unbuffered="<< unbuffered);

    HqlCallback<HqlResult, ThriftGen::Cell>
      cb(result, this, ns, hql, !noflush, !unbuffered);

    try {
      run_hql_interp(ns, hql, cb);
      //LOG_HQL_RESULT(result);
    } RETHROW("namespace=" << ns << " hql="<< hql <<" noflush="<< noflush
              << " unbuffered="<< unbuffered)

    if (!unbuffered && cb.is_scan)
      LOG_SLOW_QUERY(cb.profile_data, get_namespace(ns), hql);
    else {
      cb.hql = hql;
      cb.ns = ns;
    }

    LOG_API_FINISH;
  }

  virtual void
  hql_query(HqlResult& result, const ThriftGen::Namespace ns,
          const String &hql) {
    hql_exec(result, ns, hql, false, false);
  }

  virtual void
  hql_exec2(HqlResult2 &result, const ThriftGen::Namespace ns,
          const String &hql, bool noflush, bool unbuffered) {
    LOG_API_START("namespace=" << ns << " hql="<< hql <<" noflush="<< noflush <<
                   " unbuffered="<< unbuffered);

    HqlCallback<HqlResult2, CellAsArray>
      cb(result, this, ns, hql, !noflush, !unbuffered);

    try {
      run_hql_interp(ns, hql, cb);
      //LOG_HQL_RESULT(result);
    } RETHROW("namespace=" << ns << " hql="<< hql <<" noflush="<< noflush <<
              " unbuffered="<< unbuffered)

    if (!unbuffered && cb.is_scan)
      LOG_SLOW_QUERY(cb.profile_data, get_namespace(ns), hql);

    LOG_API_FINISH;
  }

  virtual void
  hql_exec_as_arrays(HqlResultAsArrays& result, const ThriftGen::Namespace ns,
          const String &hql, bool noflush, bool unbuffered) {
    LOG_API_START("namespace=" << ns << " hql="<< hql <<" noflush="<< noflush <<
                   " unbuffered="<< unbuffered);

    HqlCallback<HqlResultAsArrays, CellAsArray>
      cb(result, this, ns, hql, !noflush, !unbuffered);

    try {
      run_hql_interp(ns, hql, cb);
      //LOG_HQL_RESULT(result);
    } RETHROW("namespace=" << ns << " hql="<< hql <<" noflush="<< noflush <<
              " unbuffered="<< unbuffered)

    if (!unbuffered && cb.is_scan)
      LOG_SLOW_QUERY(cb.profile_data, get_namespace(ns), hql);

    LOG_API_FINISH;
  }

  virtual void
  hql_query2(HqlResult2& result, const ThriftGen::Namespace ns,
          const String &hql) {
    hql_exec2(result, ns, hql, false, false);
  }

  virtual void
  hql_query_as_arrays(HqlResultAsArrays& result, const ThriftGen::Namespace ns,
          const String &hql) {
    hql_exec_as_arrays(result, ns, hql, false, false);
  }

  virtual void namespace_create(const String &ns) {
    LOG_API_START("namespace=" << ns);
    try {
      m_context.client->create_namespace(ns, NULL);
    } RETHROW("namespace=" << ns)
    LOG_API_FINISH;
  }

  virtual void create_namespace(const String &ns) {
    namespace_create(ns);
  }

  virtual void table_create(const ThriftGen::Namespace ns, const String &table,
                            const ThriftGen::Schema &schema) {
    LOG_API_START("namespace=" << ns << " table=" << table);

    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      Hypertable::SchemaPtr hschema = new Hypertable::Schema();
      convert_schema(schema, hschema);
      namespace_ptr->create_table(table, hschema);
    } RETHROW("namespace=" << ns << " table="<< table)

    LOG_API_FINISH;
  }

  virtual void table_alter(const ThriftGen::Namespace ns, const String &table,
                           const ThriftGen::Schema &schema) {
    LOG_API_START("namespace=" << ns << " table=" << table);

    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      Hypertable::SchemaPtr hschema = new Hypertable::Schema();
      convert_schema(schema, hschema);
      namespace_ptr->alter_table(table, hschema, false);
    } RETHROW("namespace=" << ns << " table="<< table)

    LOG_API_FINISH;
  }

  virtual Scanner scanner_open(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::ScanSpec &ss) {
    Scanner id;
    LOG_API_START("namespace=" << ns << " table="<< table <<" scan_spec="<< ss);
    try {
      ScannerInfoPtr si = make_shared<ScannerInfo>(ns, table);
      convert_scan_spec(ss, si->scan_spec_builder);
      id = get_scanner_id(_open_scanner(ns, table, si->scan_spec_builder.get()), si);
    } RETHROW("namespace=" << ns << " table="<< table <<" scan_spec="<< ss)
    LOG_API_FINISH_E(" scanner="<<id);
    return id;
  }

  virtual Scanner open_scanner(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::ScanSpec &ss) {
    return scanner_open(ns, table, ss);
  }

  virtual ScannerAsync async_scanner_open(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::Future ff,
          const ThriftGen::ScanSpec &ss) {
    ScannerAsync id;
    LOG_API_START("namespace=" << ns << " table=" << table << " future="
            << ff << " scan_spec=" << ss);
    try {
      id = get_object_id(_open_scanner_async(ns, table, ff, ss));
      add_reference(id, ff);
    } RETHROW("namespace=" << ns << " table=" << table << " future="
            << ff << " scan_spec="<< ss)

    LOG_API_FINISH_E(" scanner=" << id);
    return id;
  }

  virtual ScannerAsync open_scanner_async(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::Future ff,
          const ThriftGen::ScanSpec &ss) {
    return async_scanner_open(ns, table, ff, ss);
  }

  virtual void namespace_close(const ThriftGen::Namespace ns) {
    LOG_API_START("namespace="<< ns);
    try {
      remove_namespace(ns);
    } RETHROW("namespace="<< ns)
    LOG_API_FINISH;
  }

  virtual void close_namespace(const ThriftGen::Namespace ns) {
    namespace_close(ns);
  }

  virtual void refresh_table(const ThriftGen::Namespace ns,
          const String &table_name) {
    LOG_API_START("namespace=" << ns << " table=" << table_name);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      namespace_ptr->refresh_table(table_name);
    } RETHROW("namespace=" << ns << " table=" << table_name);
    LOG_API_FINISH;
  }

  virtual void scanner_close(const Scanner id) {
    LOG_API_START("scanner="<< id);
    try {
      ClientObjectPtr cobj;
      remove_scanner(id, cobj, scanner_info);
      TableScanner *scanner = dynamic_cast<TableScanner *>(cobj.get());
      if (g_log_slow_queries) {
        ProfileDataScanner pd;
        scanner->get_profile_data(pd);
        boost::xtime_get(&end_time, TIME_UTC_);
        if (scanner_info->latency >= g_slow_query_latency_threshold) {
          if (scanner_info->hql.empty())
            log_slow_query_scanspec(__func__, start_time, end_time,
                                    scanner_info->latency, pd,
                                    get_namespace(scanner_info->ns),
                                    scanner_info->table,
                                    scanner_info->scan_spec_builder.get());
          else
            log_slow_query(__func__, start_time, end_time,
                           scanner_info->latency, pd,
                           get_namespace(scanner_info->ns),
                           scanner_info->hql);
            
        }
      }
    } RETHROW("scanner="<< id)
    LOG_API_FINISH;
  }

  virtual void close_scanner(const Scanner scanner) {
    scanner_close(scanner);
  }

  virtual void async_scanner_cancel(const ScannerAsync scanner) {
    LOG_API_START("scanner="<< scanner);

    try {
      get_scanner_async(scanner)->cancel();
    } RETHROW("scanner=" << scanner)

    LOG_API_FINISH_E(" cancelled");
  }

  virtual void cancel_scanner_async(const ScannerAsync scanner) {
    async_scanner_cancel(scanner);
  }

  virtual void async_scanner_close(const ScannerAsync scanner_async) {
    LOG_API_START("scanner_async="<< scanner_async);
    try {
      remove_scanner(scanner_async);
      remove_references(scanner_async);
    } RETHROW("scanner_async="<< scanner_async)
    LOG_API_FINISH;
  }

  virtual void close_scanner_async(const ScannerAsync scanner_async) {
    async_scanner_close(scanner_async);
  }

  virtual void scanner_get_cells(ThriftCells &result,
          const Scanner scanner_id) {
    LOG_API_START("scanner="<< scanner_id);
    try {
      TableScanner *scanner = get_scanner(scanner_id, scanner_info);
      _next(result, scanner, m_context.next_threshold);
    } RETHROW("scanner="<< scanner_id)
    LOG_API_FINISH_E(" result.size=" << result.size());
  }

  virtual void next_cells(ThriftCells &result, const Scanner scanner_id) {
    scanner_get_cells(result, scanner_id);
  }

  virtual void scanner_get_cells_as_arrays(ThriftCellsAsArrays &result,
          const Scanner scanner_id) {
    LOG_API_START("scanner="<< scanner_id);
    try {
      TableScanner *scanner = get_scanner(scanner_id, scanner_info);
      _next(result, scanner, m_context.next_threshold);
    } RETHROW("scanner="<< scanner_id <<" result.size="<< result.size())
    LOG_API_FINISH_E("result.size="<< result.size());
  }

  virtual void next_cells_as_arrays(ThriftCellsAsArrays &result,
          const Scanner scanner_id) {
    scanner_get_cells_as_arrays(result, scanner_id);
  }

  virtual void scanner_get_cells_serialized(CellsSerialized &result,
          const Scanner scanner_id) {
    LOG_API_START("scanner="<< scanner_id);

    try {
      SerializedCellsWriter writer(m_context.next_threshold);
      Hypertable::Cell cell;

      TableScanner *scanner = get_scanner(scanner_id, scanner_info);

      while (1) {
        if (scanner->next(cell)) {
          if (!writer.add(cell)) {
            writer.finalize(SerializedCellsFlag::EOB);
            scanner->unget(cell);
            break;
          }
        }
        else {
          writer.finalize(SerializedCellsFlag::EOS);
          break;
        }
      }

      result = String((char *)writer.get_buffer(), writer.get_buffer_length());
    } RETHROW("scanner="<< scanner_id);
    LOG_API_FINISH_E("result.size="<< result.size());
  }

  virtual void next_cells_serialized(CellsSerialized &result,
          const Scanner scanner_id) {
    scanner_get_cells_serialized(result, scanner_id);
  }

  virtual void scanner_get_row(ThriftCells &result, const Scanner scanner_id) {
    LOG_API_START("scanner="<< scanner_id <<" result.size="<< result.size());
    try {
      TableScanner *scanner = get_scanner(scanner_id, scanner_info);
      _next_row(result, scanner);
    } RETHROW("scanner=" << scanner_id)

    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void next_row(ThriftCells &result, const Scanner scanner_id) {
    scanner_get_row(result, scanner_id);
  }

  virtual void scanner_get_row_as_arrays(ThriftCellsAsArrays &result,
          const Scanner scanner_id) {
    LOG_API_START("scanner="<< scanner_id);
    try {
      TableScanner *scanner = get_scanner(scanner_id, scanner_info);
      _next_row(result, scanner);
    } RETHROW("result.size=" << result.size())
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void next_row_as_arrays(ThriftCellsAsArrays &result,
          const Scanner scanner_id) {
    scanner_get_row_as_arrays(result, scanner_id);
  }

  virtual void scanner_get_row_serialized(CellsSerialized &result,
          const Scanner scanner_id) {
    LOG_API_START("scanner="<< scanner_id);

    try {
      SerializedCellsWriter writer(0, true);
      Hypertable::Cell cell;
      std::string prev_row;

      TableScanner *scanner = get_scanner(scanner_id, scanner_info);

      while (1) {
        if (scanner->next(cell)) {
          // keep scanning
          if (prev_row.empty() || prev_row == cell.row_key) {
            // add cells from this row
            writer.add(cell);
            if (prev_row.empty())
              prev_row = cell.row_key;
          }
          else {
            // done with this row
            writer.finalize(SerializedCellsFlag::EOB);
            scanner->unget(cell);
            break;
          }
        }
        else {
          // done with this scan
          writer.finalize(SerializedCellsFlag::EOS);
          break;
        }
      }

      result = String((char *)writer.get_buffer(), writer.get_buffer_length());
    } RETHROW("scanner="<< scanner_id)
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void next_row_serialized(CellsSerialized& result,
          const Scanner scanner_id) {
    scanner_get_row_serialized(result, scanner_id);
  }

  virtual void get_row(ThriftCells &result, const ThriftGen::Namespace ns,
          const String &table, const String &row) {
    LOG_API_START("namespace=" << ns << " table="<< table <<" row="<< row);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      TablePtr t = namespace_ptr->open_table(table);
      Hypertable::ScanSpec ss;
      ss.row_intervals.push_back(Hypertable::RowInterval(row.c_str(), true,
                                                         row.c_str(), true));
      ss.max_versions = 1;
      TableScannerPtr scanner = t->create_scanner(ss);
      _next(result, scanner.get(), INT32_MAX);
      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, ss);
    } RETHROW("namespace=" << ns << " table="<< table <<" row="<< row)
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void get_row_as_arrays(ThriftCellsAsArrays &result,
          const ThriftGen::Namespace ns, const String &table,
          const String &row) {
    LOG_API_START("namespace=" << ns << " table="<< table <<" row="<< row);

    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      TablePtr t = namespace_ptr->open_table(table);
      Hypertable::ScanSpec ss;
      ss.row_intervals.push_back(Hypertable::RowInterval(row.c_str(), true,
                                                         row.c_str(), true));
      ss.max_versions = 1;
      TableScannerPtr scanner = t->create_scanner(ss);
      _next(result, scanner.get(), INT32_MAX);
      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, ss);
    } RETHROW("namespace=" << ns << " table="<< table <<" row="<< row)
    LOG_API_FINISH_E("result.size="<< result.size());
  }

  virtual void get_row_serialized(CellsSerialized &result,
          const ThriftGen::Namespace ns, const std::string& table,
          const std::string& row) {
    LOG_API_START("namespace=" << ns << " table="<< table <<" row"<< row);

    try {
      SerializedCellsWriter writer(0, true);
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      TablePtr t = namespace_ptr->open_table(table);
      Hypertable::ScanSpec ss;
      ss.row_intervals.push_back(Hypertable::RowInterval(row.c_str(), true,
                                                         row.c_str(), true));
      ss.max_versions = 1;
      TableScannerPtr scanner = t->create_scanner(ss);
      Hypertable::Cell cell;

      while (scanner->next(cell))
        writer.add(cell);
      writer.finalize(SerializedCellsFlag::EOS);

      result = String((char *)writer.get_buffer(), writer.get_buffer_length());
      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, ss);
    } RETHROW("namespace=" << ns << " table="<< table <<" row"<< row)
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void get_cell(Value &result, const ThriftGen::Namespace ns,
          const String &table, const String &row, const String &column) {
    LOG_API_START("namespace=" << ns << " table=" << table << " row="
            << row << " column=" << column);

    if (row.empty())
      HT_THROW(Error::BAD_KEY, "Empty row key");
      
    try {
      Hypertable::ScanSpec ss;
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      TablePtr t = namespace_ptr->open_table(table);

      ss.cell_intervals.push_back(Hypertable::CellInterval(row.c_str(),
          column.c_str(), true, row.c_str(), column.c_str(), true));
      ss.max_versions = 1;

      Hypertable::Cell cell;
      TableScannerPtr scanner = t->create_scanner(ss, 0, true);

      if (scanner->next(cell))
        result = String((char *)cell.value, cell.value_len);

      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, ss);

    } RETHROW("namespace=" << ns << " table=" << table << " row="
            << row << " column=" << column)

    LOG_API_FINISH_E(" result=" << result);
  }

  virtual void get_cells(ThriftCells &result, const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::ScanSpec &ss) {
    LOG_API_START("namespace=" << ns << " table=" << table << " scan_spec="
            << ss << " result.size=" << result.size());

    try {
      Hypertable::ScanSpec hss;
      convert_scan_spec(ss, hss);
      TableScannerPtr scanner = _open_scanner(ns, table, hss);
      _next(result, scanner.get(), INT32_MAX);
      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, hss);
    } RETHROW("namespace=" << ns << " table="<< table <<" scan_spec="<< ss)
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void get_cells_as_arrays(ThriftCellsAsArrays &result,
          const ThriftGen::Namespace ns, const String &table,
          const ThriftGen::ScanSpec &ss) {
    LOG_API_START("namespace=" << ns << " table="<< table <<" scan_spec="<< ss);

    try {
      Hypertable::ScanSpec hss;
      convert_scan_spec(ss, hss);
      TableScannerPtr scanner = _open_scanner(ns, table, hss);
      _next(result, scanner.get(), INT32_MAX);
      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, hss);
    } RETHROW("namespace=" << ns << " table="<< table <<" scan_spec="<< ss)
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void get_cells_serialized(CellsSerialized &result,
          const ThriftGen::Namespace ns, const String& table,
          const ThriftGen::ScanSpec& ss) {
    LOG_API_START("namespace=" << ns << " table="<< table <<" scan_spec="<< ss);

    try {
      Hypertable::ScanSpec hss;
      convert_scan_spec(ss, hss);
      SerializedCellsWriter writer(0, true);
      TableScannerPtr scanner = _open_scanner(ns, table, hss);
      Hypertable::Cell cell;

      while (scanner->next(cell))
        writer.add(cell);
      writer.finalize(SerializedCellsFlag::EOS);

      result = String((char *)writer.get_buffer(), writer.get_buffer_length());
      LOG_SLOW_QUERY_SCANNER(scanner, get_namespace(ns), table, hss);
    } RETHROW("namespace=" << ns << " table="<< table <<" scan_spec="<< ss)
    LOG_API_FINISH_E(" result.size="<< result.size());
  }

  virtual void shared_mutator_set_cells(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec,
          const ThriftCells &cells) {
    LOG_API_START("namespace=" << ns << " table=" << table <<
            " mutate_spec.appname=" << mutate_spec.appname);

    try {
      _offer_cells(ns, table, mutate_spec, cells);
    } RETHROW("namespace=" << ns << " table=" << table
            << " mutate_spec.appname="<< mutate_spec.appname)
    LOG_API_FINISH_E(" cells.size=" << cells.size());
  }

  virtual void offer_cells(const ThriftGen::Namespace ns, const String &table,
          const ThriftGen::MutateSpec &mutate_spec, const ThriftCells &cells) {
    shared_mutator_set_cells(ns, table, mutate_spec, cells);
  }

  virtual void shared_mutator_set_cell(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec,
          const ThriftGen::Cell &cell) {
    LOG_API_START(" namespace=" << ns << " table=" << table
            << " mutate_spec.appname="<< mutate_spec.appname);

    try {
      _offer_cell(ns, table, mutate_spec, cell);
    } RETHROW("namespace=" << ns << " table=" << table
            << " mutate_spec.appname="<< mutate_spec.appname)
    LOG_API_FINISH_E(" cell="<< cell);
  }

  virtual void offer_cell(const ThriftGen::Namespace ns, const String &table,
          const ThriftGen::MutateSpec &mutate_spec,
          const ThriftGen::Cell &cell) {
    shared_mutator_set_cell(ns, table, mutate_spec, cell);
  }

  virtual void shared_mutator_set_cells_as_arrays(const ThriftGen::Namespace ns,
         const String &table, const ThriftGen::MutateSpec &mutate_spec,
         const ThriftCellsAsArrays &cells) {
    LOG_API_START(" namespace=" << ns << " table=" << table
            << " mutate_spec.appname=" << mutate_spec.appname);
    try {
      _offer_cells(ns, table, mutate_spec, cells);
      LOG_API("mutate_spec.appname=" << mutate_spec.appname << " done");
    } RETHROW("namespace=" << ns << " table=" << table
            << " mutate_spec.appname=" << mutate_spec.appname)
    LOG_API_FINISH_E(" cells.size=" << cells.size());
  }

  virtual void offer_cells_as_arrays(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec,
          const ThriftCellsAsArrays &cells) {
    shared_mutator_set_cells_as_arrays(ns, table, mutate_spec, cells);
  }

  virtual void shared_mutator_set_cell_as_array(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec,
          const CellAsArray &cell) {
    // gcc 4.0.1 cannot seems to handle << cell here (see ThriftHelper.h)
    LOG_API_START("namespace=" << ns << " table=" << table
            << " mutate_spec.appname=" << mutate_spec.appname);

    try {
      _offer_cell(ns, table, mutate_spec, cell);
      LOG_API("mutate_spec.appname=" << mutate_spec.appname << " done");
    } RETHROW("namespace=" << ns << " table=" << table
            << " mutate_spec.appname=" << mutate_spec.appname)
    LOG_API_FINISH_E(" cell.size=" << cell.size());
  }

  virtual void offer_cell_as_array(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec,
          const CellAsArray &cell) {
    shared_mutator_set_cell_as_array(ns, table, mutate_spec, cell);
  }

  virtual ThriftGen::Future future_open(int capacity) {
    ThriftGen::Future id;
    LOG_API_START("capacity=" << capacity);
    try {
      capacity = (capacity <= 0) ? m_context.future_capacity : capacity;
      id = get_object_id( new Hypertable::Future(capacity) );
    } RETHROW("capacity=" << capacity)
    LOG_API_FINISH_E(" future=" << id);
    return id;
  }

  virtual ThriftGen::Future open_future(int capacity) {
    return future_open(capacity);
  }

  virtual void future_get_result(ThriftGen::Result &tresult,
          ThriftGen::Future ff, int timeout_millis) {
    LOG_API_START("future=" << ff);

    try {
      Hypertable::Future *future = get_future(ff);
      ResultPtr hresult;
      bool timed_out = false;
      bool done = !(future->get(hresult, (uint32_t)timeout_millis,
                  timed_out));
      if (timed_out)
        THROW_TE(Error::REQUEST_TIMEOUT, "Failed to fetch Future result");
      if (done) {
        tresult.is_empty = true;
        tresult.id = 0;
        LOG_API_FINISH_E(" is_empty="<< tresult.is_empty);
      }
      else {
        tresult.is_empty = false;
        _convert_result(hresult, tresult);
        LOG_API_FINISH_E(" is_empty=" << tresult.is_empty << " id="
                << tresult.id << " is_scan=" << tresult.is_scan
                << " is_error=" << tresult.is_error);
      }
    } RETHROW("future=" << ff)
  }

  virtual void get_future_result(ThriftGen::Result &tresult,
          ThriftGen::Future ff, int timeout_millis) {
    future_get_result(tresult, ff, timeout_millis);
  }

  virtual void future_get_result_as_arrays(ThriftGen::ResultAsArrays &tresult,
          ThriftGen::Future ff, int timeout_millis) {
    LOG_API_START("future=" << ff);
    try {
      Hypertable::Future *future = get_future(ff);
      ResultPtr hresult;
      bool timed_out = false;
      bool done = !(future->get(hresult, (uint32_t)timeout_millis,
                  timed_out));
      if (timed_out)
        THROW_TE(Error::REQUEST_TIMEOUT, "Failed to fetch Future result");
      if (done) {
        tresult.is_empty = true;
        tresult.id = 0;
        LOG_API_FINISH_E(" done="<< done );
      }
      else {
        tresult.is_empty = false;
        _convert_result_as_arrays(hresult, tresult);
        LOG_API_FINISH_E(" done=" << done << " id=" << tresult.id
                << " is_scan=" << tresult.is_scan << "is_error="
                << tresult.is_error);
      }
    } RETHROW("future=" << ff)
  }

  virtual void get_future_result_as_arrays(ThriftGen::ResultAsArrays &tresult,
          ThriftGen::Future ff, int timeout_millis) {
    future_get_result_as_arrays(tresult, ff, timeout_millis);
  }

  virtual void future_get_result_serialized(ThriftGen::ResultSerialized &tresult,
          ThriftGen::Future ff, int timeout_millis) {
    LOG_API_START("future=" << ff);

    try {
      Hypertable::Future *future = get_future(ff);
      ResultPtr hresult;
      bool timed_out = false;
      bool done = !(future->get(hresult, (uint32_t)timeout_millis,
                  timed_out));
      if (timed_out)
        THROW_TE(Error::REQUEST_TIMEOUT, "Failed to fetch Future result");
      if (done) {
        tresult.is_empty = true;
        tresult.id = 0;
        LOG_API_FINISH_E(" done="<< done );
      }
      else {
        tresult.is_empty = false;
        _convert_result_serialized(hresult, tresult);
        LOG_API_FINISH_E(" done=" << done << " id=" << tresult.id
                << " is_scan=" << tresult.is_scan << "is_error="
                << tresult.is_error);
      }
    } RETHROW("future=" << ff)
  }

  virtual void get_future_result_serialized(ThriftGen::ResultSerialized &tresult,
          ThriftGen::Future ff, int timeout_millis) {
    future_get_result_serialized(tresult, ff, timeout_millis);
  }

  virtual void future_cancel(ThriftGen::Future ff) {
    LOG_API_START("future=" << ff);

    try {
      Hypertable::Future *future = get_future(ff);
      future->cancel();
    } RETHROW("future=" << ff)
    LOG_API_FINISH;
  }

  virtual void cancel_future(ThriftGen::Future ff) {
    future_cancel(ff);
  }

  virtual bool future_is_empty(ThriftGen::Future ff) {
    LOG_API_START("future=" << ff);
    bool is_empty;
    try {
      Hypertable::Future *future = get_future(ff);
      is_empty = future->is_empty();
    } RETHROW("future=" << ff)
    LOG_API_FINISH_E(" is_empty=" << is_empty);
    return is_empty;
  }

  virtual bool future_is_full(ThriftGen::Future ff) {
    LOG_API_START("future=" << ff);
    bool full;
    try {
      Hypertable::Future *future = get_future(ff);
      full = future->is_full();
    } RETHROW("future=" << ff)
    LOG_API_FINISH_E(" full=" << full);
    return full;
  }

  virtual bool future_is_cancelled(ThriftGen::Future ff) {
    LOG_API_START("future=" << ff);
    bool cancelled;
    try {
      Hypertable::Future *future = get_future(ff);
      cancelled = future->is_cancelled();
    } RETHROW("future=" << ff)
    LOG_API_FINISH_E(" cancelled=" << cancelled);
    return cancelled;
  }

  virtual bool future_has_outstanding(ThriftGen::Future ff) {
    bool has_outstanding;
    LOG_API_START("future=" << ff);
    try {
      Hypertable::Future *future = get_future(ff);
      has_outstanding = future->has_outstanding();
    } RETHROW("future=" << ff)
    LOG_API_FINISH_E(" has_outstanding=" << has_outstanding);
    return has_outstanding;
  }

  virtual void future_close(const ThriftGen::Future ff) {
    LOG_API_START("future="<< ff);
    try {
      remove_future(ff);
    } RETHROW("future=" << ff)
    LOG_API_FINISH;
  }

  virtual void close_future(const ThriftGen::Future ff) {
    future_close(ff);
  }

  virtual ThriftGen::Namespace namespace_open(const String &ns) {
    ThriftGen::Namespace id;
    LOG_API_START("namespace =" << ns);
    try {
      id = get_cached_object_id( m_context.client->open_namespace(ns) );
    } RETHROW("namespace " << ns)
    LOG_API_FINISH_E(" id=" << id);
    return id;
  }

  virtual ThriftGen::Namespace open_namespace(const String &ns) {
    return namespace_open(ns);
  }

  virtual MutatorAsync async_mutator_open(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::Future ff, ::int32_t flags) {
    LOG_API_START("namespace=" << ns << " table=" << table << " future="
            << ff << " flags=" << flags);
    MutatorAsync id;
    try {
      id = get_object_id(_open_mutator_async(ns, table, ff, flags));
      add_reference(id, ff);
    } RETHROW("namespace=" << ns << " table=" << table << " future="
            << ff << " flags=" << flags)
    LOG_API_FINISH_E(" mutator=" << id);
    return id;
  }

  virtual MutatorAsync open_mutator_async(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::Future ff, ::int32_t flags) {
    return async_mutator_open(ns, table, ff, flags);
  }

  virtual Mutator mutator_open(const ThriftGen::Namespace ns,
          const String &table, int32_t flags, int32_t flush_interval) {
    LOG_API_START("namespace=" << ns << "table=" << table << " flags="
            << flags << " flush_interval=" << flush_interval);
    Mutator id;
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      TablePtr t = namespace_ptr->open_table(table);
      id =  get_object_id(t->create_mutator(0, flags, flush_interval));
    } RETHROW("namespace=" << ns << "table=" << table << " flags="
            << flags << " flush_interval=" << flush_interval)
    LOG_API_FINISH_E(" async_mutator=" << id);
    return id;
  }

  virtual Mutator open_mutator(const ThriftGen::Namespace ns,
          const String &table, int32_t flags, int32_t flush_interval) {
    return mutator_open(ns, table, flags, flush_interval);
  }

  virtual void mutator_flush(const Mutator mutator) {
    LOG_API_START("mutator="<< mutator);
    try {
      get_mutator(mutator)->flush();
    } RETHROW("mutator=" << mutator)
    LOG_API_FINISH_E(" done");
  }

  virtual void flush_mutator(const Mutator mutator) {
    mutator_flush(mutator);
  }

  virtual void async_mutator_flush(const MutatorAsync mutator) {
    LOG_API_START("mutator="<< mutator);
    try {
      get_mutator_async(mutator)->flush();
    } RETHROW("mutator=" << mutator)
    LOG_API_FINISH_E(" done");
  }

  virtual void flush_mutator_async(const MutatorAsync mutator) {
    async_mutator_flush(mutator);
  }

  virtual void mutator_close(const Mutator mutator) {
    LOG_API_START("mutator="<< mutator);
    try {
      flush_mutator(mutator);
      remove_mutator(mutator);
    } RETHROW("mutator=" << mutator)
    LOG_API_FINISH;
  }

  virtual void close_mutator(const Mutator mutator) {
    mutator_close(mutator);
  }

  virtual void async_mutator_cancel(const MutatorAsync mutator) {
    LOG_API_START("mutator="<< mutator);

    try {
      get_mutator_async(mutator)->cancel();
    } RETHROW("mutator="<< mutator)

    LOG_API_FINISH_E(" cancelled");
  }

  virtual void cancel_mutator_async(const MutatorAsync mutator) {
    async_mutator_cancel(mutator);
  }

  virtual void async_mutator_close(const MutatorAsync mutator) {
    LOG_API_START("mutator="<< mutator);
    try {
      flush_mutator_async(mutator);
      remove_mutator(mutator);
      remove_references(mutator);
    } RETHROW("mutator" << mutator)
    LOG_API_FINISH;
  }

  virtual void close_mutator_async(const MutatorAsync mutator) {
    async_mutator_close(mutator);
  }

  virtual void mutator_set_cells(const Mutator mutator,
          const ThriftCells &cells) {
    LOG_API_START("mutator=" << mutator << " cell.size=" << cells.size());
    try {
      _set_cells(mutator, cells);
    } RETHROW("mutator=" << mutator << " cell.size=" << cells.size())
    LOG_API_FINISH;
  }

  virtual void mutator_set_cell(const Mutator mutator,
          const ThriftGen::Cell &cell) {
    LOG_API_START("mutator=" << mutator << " cell=" << cell);
    try {
      _set_cell(mutator, cell);
    } RETHROW("mutator=" << mutator << " cell=" << cell)
    LOG_API_FINISH;
  }

  virtual void mutator_set_cells_as_arrays(const Mutator mutator,
          const ThriftCellsAsArrays &cells) {
    LOG_API_START("mutator=" << mutator << " cell.size=" << cells.size());
    try {
      _set_cells(mutator, cells);
    } RETHROW("mutator=" << mutator << " cell.size=" << cells.size())
    LOG_API_FINISH;
  }

  virtual void mutator_set_cell_as_array(const Mutator mutator,
          const CellAsArray &cell) {
    // gcc 4.0.1 cannot seems to handle << cell here (see ThriftHelper.h)
    LOG_API_START("mutator=" << mutator << " cell_as_array.size="
            << cell.size());
    try {
      _set_cell(mutator, cell);
    } RETHROW("mutator="<< mutator <<" cell_as_array.size="<< cell.size());
    LOG_API_FINISH;
  }

  virtual void mutator_set_cells_serialized(const Mutator mutator,
          const CellsSerialized &cells, const bool flush) {
    LOG_API_START("mutator=" << mutator << " cell.size=" << cells.size());
    try {
      CellsBuilder cb;
      Hypertable::Cell hcell;
      SerializedCellsReader reader((void *)cells.c_str(),
              (uint32_t)cells.length());
      while (reader.next()) {
        reader.get(hcell);
        cb.add(hcell, false);
      }
      get_mutator(mutator)->set_cells(cb.get());
      if (flush || reader.flush())
        get_mutator(mutator)->flush();
    } RETHROW("mutator="<< mutator <<" cell.size="<< cells.size())

    LOG_API_FINISH;
  }

  virtual void set_cell(const ThriftGen::Namespace ns, const String& table,
          const ThriftGen::Cell &cell) {
    LOG_API_START("ns=" << ns << " table=" << table << " cell=" << cell);
    try {
      TableMutatorPtr mutator = _open_mutator(ns, table);
      CellsBuilder cb;
      Hypertable::Cell hcell;
      convert_cell(cell, hcell);
      cb.add(hcell, false);
      mutator->set_cells(cb.get());
      mutator->flush();
    } RETHROW("ns=" << ns << " table=" << table << " cell=" << cell);
    LOG_API_FINISH;
  }

  virtual void set_cells(const ThriftGen::Namespace ns, const String& table,
          const ThriftCells &cells) {
    LOG_API_START("ns=" << ns << " table=" << table << " cell.size="
            << cells.size());
    try {
      TableMutatorPtr mutator = _open_mutator(ns, table);
      Hypertable::Cells hcells;
      convert_cells(cells, hcells);
      mutator->set_cells(hcells);
      mutator->flush();
    } RETHROW("ns=" << ns << " table=" << table <<" cell.size="
            << cells.size());
    LOG_API_FINISH;
  }

  virtual void set_cells_as_arrays(const ThriftGen::Namespace ns,
          const String& table, const ThriftCellsAsArrays &cells) {
    LOG_API_START("ns=" << ns << " table=" << table << " cell.size="
            << cells.size());

    try {
      TableMutatorPtr mutator = _open_mutator(ns, table);
      Hypertable::Cells hcells;      
      convert_cells(cells, hcells);
      mutator->set_cells(hcells);
      mutator->flush();
    } RETHROW("ns="<< ns <<" table=" << table<<" cell.size="<< cells.size());
    LOG_API_FINISH;
  }

  virtual void set_cell_as_array(const ThriftGen::Namespace ns,
          const String& table, const CellAsArray &cell) {
    // gcc 4.0.1 cannot seems to handle << cell here (see ThriftHelper.h)

    LOG_API_START("ns=" << ns << " table=" << table << " cell_as_array.size="
            << cell.size());
    try {
      TableMutatorPtr mutator = _open_mutator(ns, table);
      CellsBuilder cb;
      Hypertable::Cell hcell;
      convert_cell(cell, hcell);
      cb.add(hcell, false);
      mutator->set_cells(cb.get());
      mutator->flush();
    } RETHROW("ns=" << ns << " table=" << table << " cell_as_array.size="
            << cell.size());
    LOG_API_FINISH;
  }

  virtual void set_cells_serialized(const ThriftGen::Namespace ns,
          const String& table, const CellsSerialized &cells) {
    LOG_API_START("ns=" << ns << " table=" << table <<
            " cell_serialized.size=" << cells.size() << " flush=" << flush);
    try {
      TableMutatorPtr mutator = _open_mutator(ns, table);
      CellsBuilder cb;
      Hypertable::Cell hcell;
      SerializedCellsReader reader((void *)cells.c_str(),
              (uint32_t)cells.length());
      while (reader.next()) {
        reader.get(hcell);
        cb.add(hcell, false);
      }
      mutator->set_cells(cb.get());
      mutator->flush();
    } RETHROW("ns=" << ns << " table=" << table << " cell_serialized.size="
            << cells.size() << " flush=" << flush);

    LOG_API_FINISH;
  }

  virtual void async_mutator_set_cells(const MutatorAsync mutator,
          const ThriftCells &cells) {
    LOG_API_START("mutator=" << mutator << " cells.size=" << cells.size());
    try {
      _set_cells_async(mutator, cells);
    } RETHROW("mutator=" << mutator << " cells.size=" << cells.size())
    LOG_API_FINISH;
  }

  virtual void set_cells_async(const MutatorAsync mutator,
          const ThriftCells &cells) {
    async_mutator_set_cells(mutator, cells);
  }

  virtual void async_mutator_set_cell(const MutatorAsync mutator,
          const ThriftGen::Cell &cell) {
    LOG_API_START("mutator=" << mutator <<" cell=" << cell);
    try {
      _set_cell_async(mutator, cell);
    } RETHROW("mutator=" << mutator << " cell=" << cell);
    LOG_API_FINISH;
  }

  virtual void set_cell_async(const MutatorAsync mutator,
          const ThriftGen::Cell &cell) {
    async_mutator_set_cell(mutator, cell);
  }

  virtual void async_mutator_set_cells_as_arrays(const MutatorAsync mutator,
          const ThriftCellsAsArrays &cells) {
    LOG_API_START("mutator=" << mutator << " cells.size=" << cells.size());
    try {
      _set_cells_async(mutator, cells);
    } RETHROW("mutator=" << mutator << " cells.size=" << cells.size())
    LOG_API_FINISH;
  }

  virtual void set_cells_as_arrays_async(const MutatorAsync mutator,
          const ThriftCellsAsArrays &cells) {
    async_mutator_set_cells_as_arrays(mutator, cells);
  }

  virtual void async_mutator_set_cell_as_array(const MutatorAsync mutator,
          const CellAsArray &cell) {
    // gcc 4.0.1 cannot seems to handle << cell here (see ThriftHelper.h)
    LOG_API_START("mutator=" << mutator << " cell_as_array.size="
           << cell.size());
    try {
      _set_cell_async(mutator, cell);
    } RETHROW("mutator=" << mutator << " cell_as_array.size=" << cell.size())
    LOG_API_FINISH;
  }

  virtual void set_cell_as_array_async(const MutatorAsync mutator,
          const CellAsArray &cell) {
    async_mutator_set_cell_as_array(mutator, cell);
  }

  virtual void async_mutator_set_cells_serialized(const MutatorAsync mutator,
          const CellsSerialized &cells,
      const bool flush) {
   LOG_API_START("mutator=" << mutator << " cells.size=" << cells.size());
    try {
      CellsBuilder cb;
      Hypertable::Cell hcell;
      SerializedCellsReader reader((void *)cells.c_str(),
              (uint32_t)cells.length());
      while (reader.next()) {
        reader.get(hcell);
        cb.add(hcell, false);
      }
      TableMutatorAsync *mutator_ptr = get_mutator_async(mutator);
	    mutator_ptr->set_cells(cb.get());
      if (flush || reader.flush() || mutator_ptr->needs_flush())
        mutator_ptr->flush();

    } RETHROW("mutator=" << mutator << " cells.size=" << cells.size());
    LOG_API_FINISH;
  }

  virtual void set_cells_serialized_async(const MutatorAsync mutator,
          const CellsSerialized &cells,
      const bool flush) {
    async_mutator_set_cells_serialized(mutator, cells, flush);
  }

  virtual bool namespace_exists(const String &ns) {
    bool exists;
    LOG_API_START("namespace=" << ns);
    try {
      exists = m_context.client->exists_namespace(ns);
    } RETHROW("namespace=" << ns)
    LOG_API_FINISH_E(" exists=" << exists);
    return exists;
  }

  virtual bool exists_namespace(const String &ns) {
    return namespace_exists(ns);
  }

  virtual bool table_exists(const ThriftGen::Namespace ns,
          const String &table) {
    LOG_API_START("namespace=" << ns << " table=" << table);
    bool exists;
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      exists = namespace_ptr->exists_table(table);
    } RETHROW("namespace=" << ns << " table="<< table)
    LOG_API_FINISH_E(" exists=" << exists);
    return exists;
  }

  virtual bool exists_table(const ThriftGen::Namespace ns,
          const String &table) {
    return table_exists(ns, table);
  }

  virtual void table_get_id(String &result, const ThriftGen::Namespace ns,
          const String &table) {
    LOG_API_START("namespace=" << ns << " table=" << table);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      result = namespace_ptr->get_table_id(table);
    } RETHROW("namespace=" << ns << " table="<< table)
    LOG_API_FINISH_E(" id=" << result);
  }

  virtual void get_table_id(String &result, const ThriftGen::Namespace ns,
          const String &table) {
    table_get_id(result, ns, table);
  }

  virtual void table_get_schema_str(String &result,
          const ThriftGen::Namespace ns, const String &table) {
    LOG_API_START("namespace=" << ns << " table=" << table);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      result = namespace_ptr->get_schema_str(table);
    } RETHROW("namespace=" << ns << " table=" << table)
    LOG_API_FINISH_E(" schema=" << result);
  }

  virtual void get_schema_str(String &result, const ThriftGen::Namespace ns,
          const String &table) {
    table_get_schema_str(result, ns, table);
  }

  virtual void table_get_schema_str_with_ids(String &result,
          const ThriftGen::Namespace ns, const String &table) {
    LOG_API_START("namespace=" << ns << " table=" << table);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      result = namespace_ptr->get_schema_str(table, true);
    } RETHROW("namespace=" << ns << " table=" << table)
    LOG_API_FINISH_E(" schema=" << result);
  }

  virtual void get_schema_str_with_ids(String &result,
          const ThriftGen::Namespace ns, const String &table) {
    table_get_schema_str_with_ids(result, ns, table);
  }

  virtual void table_get_schema(ThriftGen::Schema &result,
          const ThriftGen::Namespace ns, const String &table) {
    LOG_API_START("namespace=" << ns << " table=" << table);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      Hypertable::SchemaPtr schema = namespace_ptr->get_schema(table);
      if (schema)
        convert_schema(schema, result);
    } RETHROW("namespace=" << ns << " table="<< table)
    LOG_API_FINISH;
  }

  virtual void get_schema(ThriftGen::Schema &result,
          const ThriftGen::Namespace ns, const String &table) {
    table_get_schema(result, ns, table);
  }

  virtual void get_tables(std::vector<String> &tables,
          const ThriftGen::Namespace ns) {
    LOG_API_START("namespace=" << ns);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      std::vector<Hypertable::NamespaceListing> listing;
      namespace_ptr->get_listing(false, listing);

      for(size_t ii=0; ii < listing.size(); ++ii)
        if (!listing[ii].is_namespace)
          tables.push_back(listing[ii].name);

    }
    RETHROW("namespace=" << ns)
    LOG_API_FINISH_E(" tables.size=" << tables.size());
  }

  virtual void namespace_get_listing(std::vector<ThriftGen::NamespaceListing>& _return,
          const ThriftGen::Namespace ns) {
    LOG_API_START("namespace=" << ns);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      std::vector<Hypertable::NamespaceListing> listing;
      namespace_ptr->get_listing(false, listing);
      ThriftGen::NamespaceListing entry;

      for(size_t ii=0; ii < listing.size(); ++ii) {
        entry.name = listing[ii].name;
        entry.is_namespace = listing[ii].is_namespace;
        _return.push_back(entry);
      }
    }
    RETHROW("namespace=" << ns)

    LOG_API_FINISH_E(" listing.size=" << _return.size());
  }

  virtual void get_listing(std::vector<ThriftGen::NamespaceListing>& _return,
          const ThriftGen::Namespace ns) {
    namespace_get_listing(_return, ns);
  }

  virtual void table_get_splits(std::vector<ThriftGen::TableSplit> & _return,
          const ThriftGen::Namespace ns,  const String &table) {
    TableSplitsContainer splits;
    LOG_API_START("namespace=" << ns << " table=" << table
            << " splits.size=" << _return.size());
    try {
      ThriftGen::TableSplit tsplit;
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      namespace_ptr->get_table_splits(table, splits);
      for (TableSplitsContainer::iterator iter = splits.begin();
              iter != splits.end(); ++iter) {
        convert_table_split(*iter, tsplit);
        _return.push_back(tsplit);
      }
    }
    RETHROW("namespace=" << ns << " table=" << table)

    LOG_API_FINISH_E(" splits.size=" << splits.size());
  }

  virtual void get_table_splits(std::vector<ThriftGen::TableSplit> & _return,
          const ThriftGen::Namespace ns,  const String &table) {
    table_get_splits(_return, ns, table);
  }

  virtual void namespace_drop(const String &ns, const bool if_exists) {
    LOG_API_START("namespace=" << ns << " if_exists=" << if_exists);
    try {
      m_context.client->drop_namespace(ns, NULL, if_exists);
    }
    RETHROW("namespace=" << ns << " if_exists=" << if_exists)
    LOG_API_FINISH;
  }

  virtual void drop_namespace(const String &ns, const bool if_exists) {
    namespace_drop(ns, if_exists);
  }

  virtual void table_rename(const ThriftGen::Namespace ns, const String &table,
          const String &new_table_name) {
    LOG_API_START("namespace=" << ns << " table=" << table
            << " new_table_name=" << new_table_name << " done");
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      namespace_ptr->rename_table(table, new_table_name);
    }
    RETHROW("namespace=" << ns << " table=" << table << " new_table_name="
            << new_table_name << " done")
    LOG_API_FINISH;
  }

  virtual void rename_table(const ThriftGen::Namespace ns, const String &table,
          const String &new_table_name) {
    table_rename(ns, table, new_table_name);
  }

  virtual void table_drop(const ThriftGen::Namespace ns, const String &table,
          const bool if_exists) {
    LOG_API_START("namespace=" << ns << " table=" << table << " if_exists="
            << if_exists);
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      namespace_ptr->drop_table(table, if_exists);
    }
    RETHROW("namespace=" << ns << " table=" << table << " if_exists="
            << if_exists)
    LOG_API_FINISH;
  }

  virtual void drop_table(const ThriftGen::Namespace ns, const String &table,
          const bool if_exists) {
    table_drop(ns, table, if_exists);
  }

  virtual void generate_guid(std::string& _return) {
    LOG_API_START("");
    try {
      _return = HyperAppHelper::generate_guid();
    }
    RETHROW("")
    LOG_API_FINISH;
  }

  virtual void create_cell_unique(std::string &_return,
          const ThriftGen::Namespace ns, const std::string& table_name, 
          const ThriftGen::Key& tkey, const std::string& value) {
    LOG_API_START("namespace=" << ns << " table=" << table_name 
            << tkey << " value=" << value);
    std::string guid;
    try {
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      Hypertable::KeySpec hkey;
      convert_key(tkey, hkey);
      TablePtr t = namespace_ptr->open_table(table_name);
      HyperAppHelper::create_cell_unique(t, hkey, 
              value.empty() ? guid : (std::string &)value);
    }
    RETHROW("namespace=" << ns << " table=" << table_name 
            << tkey << " value=" << value);
    LOG_API_FINISH;
    _return = value.empty() ? guid : value;
  }

  void status(ThriftGen::Status& _return) override {
    try {
      _return.__set_code(0);
      _return.__set_text("");
    }
    RETHROW("");
  }

  void shutdown() override {
    kill(getpid(), SIGKILL);
  }

  virtual void error_get_text(std::string &_return, int error_code) {
    LOG_API_START("error_code=" << error_code);
    _return = HyperAppHelper::error_get_text(error_code);
    LOG_API_FINISH;
  }

  // helper methods
  void _convert_result(const Hypertable::ResultPtr &hresult,
          ThriftGen::Result &tresult) {
  Hypertable::Cells hcells;

    if (hresult->is_scan()) {
      tresult.is_scan = true;
      tresult.id = try_get_object_id(hresult->get_scanner());
      if (hresult->is_error()) {
        tresult.is_error = true;
        hresult->get_error(tresult.error, tresult.error_msg);
        tresult.__isset.error = true;
        tresult.__isset.error_msg = true;
      }
      else {
        tresult.is_error = false;
        tresult.__isset.cells = true;
        hresult->get_cells(hcells);
        convert_cells(hcells, tresult.cells);
      }
    }
    else {
      tresult.is_scan = false;
      tresult.id = try_get_object_id(hresult->get_mutator());
      if (hresult->is_error()) {
        tresult.is_error = true;
        hresult->get_error(tresult.error, tresult.error_msg);
        hresult->get_failed_cells(hcells);
        convert_cells(hcells, tresult.cells);
        tresult.__isset.error = true;
        tresult.__isset.error_msg = true;
      }
    }
  }

  void _convert_result_as_arrays(const Hypertable::ResultPtr &hresult,
      ThriftGen::ResultAsArrays &tresult) {
  Hypertable::Cells hcells;

    if (hresult->is_scan()) {
      tresult.is_scan = true;
      tresult.id = try_get_object_id(hresult->get_scanner());
      if (hresult->is_error()) {
        tresult.is_error = true;
        hresult->get_error(tresult.error, tresult.error_msg);
        tresult.__isset.error = true;
        tresult.__isset.error_msg = true;
      }
      else {
        tresult.is_error = false;
        tresult.__isset.cells = true;
        hresult->get_cells(hcells);
        convert_cells(hcells, tresult.cells);
      }
    }
    else {
      HT_THROW(Error::NOT_IMPLEMENTED, "Support for asynchronous mutators "
              "not yet implemented");
    }
  }

  void _convert_result_serialized(Hypertable::ResultPtr &hresult,
          ThriftGen::ResultSerialized &tresult) {
  Hypertable::Cells hcells;

    if (hresult->is_scan()) {
      tresult.is_scan = true;
      tresult.id = try_get_object_id(hresult->get_scanner());
      if (hresult->is_error()) {
        tresult.is_error = true;
        hresult->get_error(tresult.error, tresult.error_msg);
        tresult.__isset.error = true;
        tresult.__isset.error_msg = true;
      }
      else {
        tresult.is_error = false;
        tresult.__isset.cells = true;
        hresult->get_cells(hcells);
        convert_cells(hcells, tresult.cells);
      }
    }
    else {
      HT_THROW(Error::NOT_IMPLEMENTED, "Support for asynchronous mutators "
              "not yet implemented");
    }
  }

  TableMutatorAsync *_open_mutator_async(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::Future ff, ::int32_t flags) {
    Hypertable::Namespace *namespace_ptr = get_namespace(ns);
    TablePtr t = namespace_ptr->open_table(table);
    Hypertable::Future *future = get_future(ff);

    return t->create_mutator_async(future, 0, flags);
  }

  TableScannerAsync *_open_scanner_async(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::Future ff,
          const ThriftGen::ScanSpec &ss) {
    Hypertable::Namespace *namespace_ptr = get_namespace(ns);
    TablePtr t = namespace_ptr->open_table(table);
    Hypertable::Future *future = get_future(ff);

    Hypertable::ScanSpec hss;
    convert_scan_spec(ss, hss);
    return t->create_scanner_async(future, hss, 0);
  }

  TableScanner *_open_scanner(const ThriftGen::Namespace ns,
          const String &table, const Hypertable::ScanSpec &ss) {
    Hypertable::Namespace *namespace_ptr = get_namespace(ns);
    TablePtr t = namespace_ptr->open_table(table);
    return t->create_scanner(ss, 0);
  }

  TableMutator *_open_mutator(const ThriftGen::Namespace ns,
          const String &table) {
    Hypertable::Namespace *namespace_ptr = get_namespace(ns);
    TablePtr t = namespace_ptr->open_table(table);
    return t->create_mutator();
  }

  template <class CellT>
  void _next(vector<CellT> &result, TableScanner *scanner, int limit) {
    Hypertable::Cell cell;
    int32_t amount_read = 0;

    while (amount_read < limit) {
      if (scanner->next(cell)) {
        CellT tcell;
        amount_read += convert_cell(cell, tcell);
        result.push_back(tcell);
      }
      else
        break;
    }
  }

  template <class CellT>
  void _next_row(vector<CellT> &result, TableScanner *scanner) {
    Hypertable::Cell cell;
    std::string prev_row;

    while (scanner->next(cell)) {
      if (prev_row.empty() || prev_row == cell.row_key) {
        CellT tcell;
        convert_cell(cell, tcell);
        result.push_back(tcell);
        if (prev_row.empty())
          prev_row = cell.row_key;
      }
      else {
        scanner->unget(cell);
        break;
      }
    }
  }

  void run_hql_interp(const ThriftGen::Namespace ns, const String &hql,
          HqlInterpreter::Callback &cb) {
    Hypertable::Namespace *namespace_ptr = get_namespace(ns);
    HqlInterpreterPtr interp = m_context.client->create_hql_interpreter(true);
    interp->set_namespace(namespace_ptr->get_name());
    interp->execute(hql, cb);
  }

  template <class CellT>
  void _offer_cells(const ThriftGen::Namespace ns, const String &table,
          const ThriftGen::MutateSpec &mutate_spec,
          const vector<CellT> &cells) {
    Hypertable::Cells hcells;
    convert_cells(cells, hcells);
    get_shared_mutator(ns, table, mutate_spec)->set_cells(hcells);
  }

  template <class CellT>
  void _offer_cell(const ThriftGen::Namespace ns, const String &table,
          const ThriftGen::MutateSpec &mutate_spec, const CellT &cell) {
    CellsBuilder cb;
    Hypertable::Cell hcell;
    convert_cell(cell, hcell);
    cb.add(hcell, false);
    get_shared_mutator(ns, table, mutate_spec)->set_cells(cb.get());
  }

  template <class CellT>
  void _set_cells(const Mutator mutator, const vector<CellT> &cells) {
    Hypertable::Cells hcells;
    convert_cells(cells, hcells);
    get_mutator(mutator)->set_cells(hcells);
  }

  template <class CellT>
  void _set_cell(const Mutator mutator, const CellT &cell) {
    CellsBuilder cb;
    Hypertable::Cell hcell;
    convert_cell(cell, hcell);
    cb.add(hcell, false);
    get_mutator(mutator)->set_cells(cb.get());
  }

  template <class CellT>
  void _set_cells_async(const MutatorAsync mutator, const vector<CellT> &cells) {
    Hypertable::Cells hcells;
    convert_cells(cells, hcells);
    TableMutatorAsync *mutator_ptr = get_mutator_async(mutator);
    mutator_ptr->set_cells(hcells);
    if (mutator_ptr->needs_flush())
      mutator_ptr->flush();
  }

  template <class CellT>
  void _set_cell_async(const MutatorAsync mutator, const CellT &cell) {
    CellsBuilder cb;
    Hypertable::Cell hcell;
    convert_cell(cell, hcell);
    cb.add(hcell, false);
    TableMutatorAsync *mutator_ptr = get_mutator_async(mutator);
    mutator_ptr->set_cells(cb.get());
    if (mutator_ptr->needs_flush())
      mutator_ptr->flush();
  }

  ClientObject *get_object(int64_t id) {
    ScopedLock lock(m_mutex);
    ObjectMap::iterator it = m_object_map.find(id);
    return (it != m_object_map.end()) ? it->second.get() : 0;
  }

  ClientObject *get_cached_object(int64_t id) {
    ScopedLock lock(m_mutex);
    ObjectMap::iterator it = m_cached_object_map.find(id);
    return (it != m_cached_object_map.end()) ? it->second.get() : 0;
  }

  Hypertable::Future *get_future(int64_t id) {
    Hypertable::Future *future = dynamic_cast<Hypertable::Future *>(get_object(id));
    if (future == 0) {
      HT_ERROR_OUT << "Bad future id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_FUTURE_ID,
               format("Invalid future id: %lld", (Lld)id));
    }
    return future;
  }


  Hypertable::Namespace *get_namespace(int64_t id) {
    Hypertable::Namespace *ns = dynamic_cast<Hypertable::Namespace *>(get_cached_object(id));
    if (ns == 0) {
      HT_ERROR_OUT << "Bad namespace id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_NAMESPACE_ID,
               format("Invalid namespace id: %lld", (Lld)id));
    }
    return ns;
  }

  int64_t get_cached_object_id(ClientObjectPtr co) {
    int64_t id;
    ScopedLock lock(m_mutex);
    while (!m_cached_object_map.insert(make_pair(id = Random::number32(), co)).second || id == 0); // no overwrite
    return id;
  }

  int64_t get_object_id(ClientObjectPtr co) {
    ScopedLock lock(m_mutex);
    int64_t id = reinterpret_cast<int64_t>(co.get());
    m_object_map.insert(make_pair(id, co)); // no overwrite
    return id;
  }

  int64_t try_get_object_id(ClientObject* co) {
    ScopedLock lock(m_mutex);
    int64_t id = reinterpret_cast<int64_t>(co);
    return m_object_map.find(id) != m_object_map.end() ? id : 0;
  }

  int64_t get_scanner_id(TableScanner *scanner, ScannerInfoPtr &info) {
    ScopedLock lock(m_mutex);
    int64_t id = reinterpret_cast<int64_t>(scanner);
    m_object_map.insert(make_pair(id, scanner));
    m_scanner_info_map.insert(make_pair(id, info));
    return id;
  }

  void add_reference(int64_t from, int64_t to) {
    ScopedLock lock(m_mutex);
    ObjectMap::iterator it = m_object_map.find(to);
    ClientObject *obj = (it != m_object_map.end()) ? it->second.get() : 0;
    m_reference_map.insert(make_pair(from, obj));
  }

  void remove_references(int64_t id) {
    ScopedLock lock(m_mutex);
    m_reference_map.erase(id);
  }

  TableScannerAsync *get_scanner_async(int64_t id) {
    TableScannerAsync *scanner = 
      dynamic_cast<TableScannerAsync *>(get_object(id));
    if (scanner == 0) {
      HT_ERROR_OUT << "Bad scanner id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_SCANNER_ID,
               format("Invalid scanner id: %lld", (Lld)id));
    }
    return scanner;
  }

  TableScanner *get_scanner(int64_t id, ScannerInfoPtr &info) {
    ScopedLock lock(m_mutex);
    auto it = m_object_map.find(id);
    if (it == m_object_map.end()) {
      HT_ERROR_OUT << "Bad scanner id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_SCANNER_ID,
               format("Invalid scanner id: %lld", (Lld)id));
    }
    auto sit = m_scanner_info_map.find(id);
    HT_ASSERT(sit != m_scanner_info_map.end());
    info = sit->second;
    return dynamic_cast<TableScanner *>(it->second.get());
  }

  bool remove_object(int64_t id) {
    // destroy client object unlocked
    bool removed = false;
    ClientObjectPtr item;
    {
      ScopedLock lock(m_mutex);
      ObjectMap::iterator it = m_object_map.find(id);
      if (it != m_object_map.end()) {
        item = (*it).second;
        m_object_map.erase(it);
        removed = true;
      }
    }
    return removed;
  }

  bool remove_cached_object(int64_t id) {
    // destroy client object unlocked
    bool removed = false;
    ClientObjectPtr item;
    {
      ScopedLock lock(m_mutex);
      ObjectMap::iterator it = m_cached_object_map.find(id);
      if (it != m_cached_object_map.end()) {
        item = (*it).second;
        m_cached_object_map.erase(it);
        removed = true;
      }
    }
    return removed;
  }

  void remove_scanner(int64_t id) {
    // destroy client object unlocked
    ClientObjectPtr item;
    {
      ScopedLock lock(m_mutex);
      m_scanner_info_map.erase(id);
      ObjectMap::iterator it = m_object_map.find(id);
      if (it != m_object_map.end()) {
        item = (*it).second;
        m_object_map.erase(it);
      }
      else {
        HT_ERROR_OUT << "Bad scanner id - " << id << HT_END;
        THROW_TE(Error::THRIFTBROKER_BAD_SCANNER_ID,
                 format("Invalid scanner id: %lld", (Lld)id));
      }
    }
  }

  void remove_scanner(int64_t id, ClientObjectPtr &scanner, ScannerInfoPtr &info) {
    ScopedLock lock(m_mutex);
    ObjectMap::iterator it = m_object_map.find(id);
    if (it != m_object_map.end()) {
      scanner = (*it).second;
      m_object_map.erase(it);
    }
    else {
      HT_ERROR_OUT << "Bad scanner id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_SCANNER_ID,
               format("Invalid scanner id: %lld", (Lld)id));
    }
    info = m_scanner_info_map[id];
    m_scanner_info_map.erase(id);
  }

  virtual void shared_mutator_refresh(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec) {
    ScopedLock lock(m_context.shared_mutator_mutex);
    SharedMutatorMapKey skey(get_namespace(ns), table, mutate_spec);

    SharedMutatorMap::iterator it = m_context.shared_mutator_map.find(skey);

    // if mutator exists then delete it
    if (it != m_context.shared_mutator_map.end()) {
      LOG_API("deleting shared mutator on namespace=" << ns << " table="
              << table << " with appname=" << mutate_spec.appname);
      m_context.shared_mutator_map.erase(it);
    }

    //re-create the shared mutator
    // else create it and insert it in the map
    LOG_API("creating shared mutator on namespace=" << ns << " table=" << table
            <<" with appname=" << mutate_spec.appname);
    Hypertable::Namespace *namespace_ptr = get_namespace(ns);
    TablePtr t = namespace_ptr->open_table(table);
    TableMutator *mutator = t->create_mutator(0, mutate_spec.flags,
            mutate_spec.flush_interval);
    m_context.shared_mutator_map[skey] = mutator;
    return;
  }

  virtual void refresh_shared_mutator(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec) {
    shared_mutator_refresh(ns, table, mutate_spec);
  }

  TableMutator *get_shared_mutator(const ThriftGen::Namespace ns,
          const String &table, const ThriftGen::MutateSpec &mutate_spec) {
    ScopedLock lock(m_context.shared_mutator_mutex);
    SharedMutatorMapKey skey(get_namespace(ns), table, mutate_spec);

    SharedMutatorMap::iterator it = m_context.shared_mutator_map.find(skey);

    // if mutator exists then return it
    if (it != m_context.shared_mutator_map.end())
      return it->second;
    else {
      // else create it and insert it in the map
      LOG_API("creating shared mutator on namespace=" << ns << " table="
              << table << " with appname=" << mutate_spec.appname);
      Hypertable::Namespace *namespace_ptr = get_namespace(ns);
      TablePtr t = namespace_ptr->open_table(table);
      TableMutator *mutator = t->create_mutator(0, mutate_spec.flags,
              mutate_spec.flush_interval);
      m_context.shared_mutator_map[skey] = mutator;
      return mutator;
    }
  }

  TableMutator *get_mutator(int64_t id) {
    TableMutator *mutator = dynamic_cast<TableMutator *>(get_object(id));
    if (mutator == 0) {
      HT_ERROR_OUT << "Bad mutator id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_MUTATOR_ID,
               format("Invalid mutator id: %lld", (Lld)id));
    }
    return mutator;
  }

  TableMutatorAsync *get_mutator_async(int64_t id) {
    TableMutatorAsync *mutator =
      dynamic_cast<TableMutatorAsync *>(get_object(id));
    if (mutator == 0) {
      HT_ERROR_OUT << "Bad mutator id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_MUTATOR_ID,
               format("Invalid mutator id: %lld", (Lld)id));
    }
    return mutator;
  }

  void remove_future(int64_t id) {
    if (!remove_object(id)) {
      HT_ERROR_OUT << "Bad future id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_FUTURE_ID,
               format("Invalid future id: %lld", (Lld)id));
    }
  }

  void remove_namespace(int64_t id) {
    if (!remove_cached_object(id)) {
      HT_ERROR_OUT << "Bad namespace id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_NAMESPACE_ID,
               format("Invalid namespace id: %lld", (Lld)id));
    }
  }

  void remove_mutator(int64_t id) {
    if (!remove_object(id)) {
      HT_ERROR_OUT << "Bad mutator id - " << id << HT_END;
      THROW_TE(Error::THRIFTBROKER_BAD_MUTATOR_ID,
               format("Invalid mutator id: %lld", (Lld)id));
    }
  }

private:
  String m_remote_peer;
  Context &m_context;
  Mutex m_mutex;
  multimap< ::int64_t, ClientObjectPtr> m_reference_map;
  ObjectMap m_object_map;
  ObjectMap m_cached_object_map;
  std::unordered_map< ::int64_t, ScannerInfoPtr> m_scanner_info_map;
};

template <class ResultT, class CellT>
void HqlCallback<ResultT, CellT>::on_return(const String &ret) {
  result.results.push_back(ret);
  result.__isset.results = true;
}

template <class ResultT, class CellT>
void HqlCallback<ResultT, CellT>::on_scan(TableScanner &s) {
  if (buffered) {
    Hypertable::Cell hcell;
    CellT tcell;

    while (s.next(hcell)) {
      convert_cell(hcell, tcell);
      result.cells.push_back(tcell);
    }
    result.__isset.cells = true;

    if (g_log_slow_queries)
      s.get_profile_data(profile_data);

  }
  else {
    ScannerInfoPtr si = make_shared<ScannerInfo>(ns);
    si->hql = hql;
    result.scanner = handler.get_scanner_id(&s, si);
    result.__isset.scanner = true;
  }
  is_scan = true;
}

template <class ResultT, class CellT>
void HqlCallback<ResultT, CellT>::on_finish(TableMutator *m) {
  if (flush) {
    Parent::on_finish(m);
  }
  else if (m) {
    result.mutator = handler.get_object_id(m);
    result.__isset.mutator = true;
  }
}

namespace {
  Context *g_context = 0;
}

class ServerHandlerFactory {
public:

  static ServerHandler* getHandler(const String& remotePeer) {
    return instance.get_handler(remotePeer);
  }

  static void releaseHandler(ServerHandler* serverHandler) {
    try {
      instance.release_handler(serverHandler);
    }
    catch (Hypertable::Exception &e) {
      HT_ERRORF("%s - %s", Error::get_text(e.code()), e.what());
    }
  }

private:

  ServerHandlerFactory() { }

  ServerHandler* get_handler(const String& remotePeer) {
    ScopedLock lock(m_mutex);
    ServerHandlerMap::iterator it = m_server_handler_map.find(remotePeer);
    if (it != m_server_handler_map.end()) {
      ++it->second.first;
      return it->second.second;
    }

    ServerHandler* serverHandler = new ServerHandler(remotePeer, *g_context);
    m_server_handler_map.insert(
      std::make_pair(remotePeer,
      std::make_pair(1, serverHandler)));

    return serverHandler;
  }

  void release_handler(ServerHandler* serverHandler) {
    {
      ScopedLock lock(m_mutex);
      ServerHandlerMap::iterator it =
        m_server_handler_map.find(serverHandler->remote_peer());
      if (it != m_server_handler_map.end()) {
        if (--it->second.first > 0) {
          return;
        }
      }
      m_server_handler_map.erase(it);
    }
    delete serverHandler;
  }

  Mutex m_mutex;
  typedef std::map<String, std::pair<int, ServerHandler*> > ServerHandlerMap;
  ServerHandlerMap m_server_handler_map;
  static ServerHandlerFactory instance;
};

ServerHandlerFactory ServerHandlerFactory::instance;

class ThriftBrokerIfFactory : public HqlServiceIfFactory {
public:
  virtual ~ThriftBrokerIfFactory() {}

  virtual HqlServiceIf* getHandler(const ::apache::thrift::TConnectionInfo& connInfo) {
    typedef ::apache::thrift::transport::TSocket TTransport;
    String remotePeer =
      dynamic_cast<TTransport*>(connInfo.transport.get())->getPeerAddress();
    g_metrics_handler->connection_increment();
    return ServerHandlerFactory::getHandler(remotePeer);
  }

  virtual void releaseHandler( ::Hypertable::ThriftGen::ClientServiceIf *service) {
    ServerHandler* serverHandler = dynamic_cast<ServerHandler*>(service);
    g_metrics_handler->connection_decrement();
    return ServerHandlerFactory::releaseHandler(serverHandler);
  }
};


}} // namespace Hypertable::ThriftBroker


int main(int argc, char **argv) {
  using namespace Hypertable;
  using namespace ThriftBroker;
  Random::seed(time(NULL));

  try {
    init_with_policies<Policies>(argc, argv);

    if (get_bool("ThriftBroker.Hyperspace.Session.Reconnect"))
      properties->set("Hyperspace.Session.Reconnect", true);

    if (get_bool("ThriftBroker.SlowQueryLog.Enable")) {
      g_log_slow_queries = true;
      g_slow_query_latency_threshold = get_i32("ThriftBroker.SlowQueryLog.LatencyThreshold");
      g_slow_query_log = new Cronolog("SlowQuery.log",
                                      System::install_dir + "/log",
                                      System::install_dir + "/log/archive");
    }

    g_metrics_handler = std::make_shared<MetricsHandler>(properties, g_slow_query_log);
    g_metrics_handler->start_collecting();

    boost::shared_ptr<ThriftBroker::Context> context(new ThriftBroker::Context());

    g_context = context.get();

    ::uint16_t port = get_i16("port");
    boost::shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());
    boost::shared_ptr<HqlServiceIfFactory> hql_service_factory(new ThriftBrokerIfFactory());
    boost::shared_ptr<TProcessorFactory> hql_service_processor_factory(new HqlServiceProcessorFactory(hql_service_factory));

    boost::shared_ptr<TServerTransport> serverTransport;

    if (has("thrift-timeout")) {
      int timeout_ms = get_i32("thrift-timeout");
      serverTransport.reset( new TServerSocket(port, timeout_ms, timeout_ms) );
    }
    else
      serverTransport.reset( new TServerSocket(port) );

    boost::shared_ptr<TTransportFactory> transportFactory(new TFramedTransportFactory());

    TThreadedServer server(hql_service_processor_factory, serverTransport,
                           transportFactory, protocolFactory);

    HT_INFO("Starting the server...");

    server.serve();

    g_metrics_handler->start_collecting();
    g_metrics_handler.reset();

    HT_INFO("Exiting.\n");
  }
  catch (Hypertable::Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }
  return 0;
}

/// @}
