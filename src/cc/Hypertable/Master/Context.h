/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#ifndef HYPERTABLE_CONTEXT_H
#define HYPERTABLE_CONTEXT_H

#include <set>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/thread/condition.hpp>

#include "Common/Filesystem.h"
#include "Common/Properties.h"
#include "Common/ReferenceCount.h"
#include "Common/StringExt.h"

#include "AsyncComm/Comm.h"
#include "AsyncComm/ConnectionManager.h"

#include "Hyperspace/Session.h"

#include "Hypertable/Lib/NameIdMapper.h"
#include "Hypertable/Lib/MetaLogDefinition.h"
#include "Hypertable/Lib/MetaLogWriter.h"
#include "Hypertable/Lib/Table.h"

#include "Monitoring.h"
#include "RangeServerConnection.h"

namespace Hypertable {

  using namespace boost::multi_index;

  class LoadBalancer;
  class Operation;
  class OperationProcessor;
  class OperationBalance;
  class ResponseManager;
  class ReferenceManager;

  class Context : public ReferenceCount {
  public:
    Context() : timer_interval(0), monitoring_interval(0), gc_interval(0),
                next_monitoring_time(0), next_gc_time(0), conn_count(0),
                test_mode(false), in_operation(false) {
      m_server_list_iter = m_server_list.end();
      master_file_handle = 0;
      balancer = 0;
      response_manager = 0;
      reference_manager = 0;
      op = 0;
      op_balance = 0;
    }
    ~Context();

    Mutex mutex;
    boost::condition cond;
    Comm *comm;
    PropertiesPtr props;
    ConnectionManagerPtr conn_manager;
    Hyperspace::SessionPtr hyperspace;
    uint64_t master_file_handle;
    FilesystemPtr dfs;
    String toplevel_dir;
    NameIdMapperPtr namemap;
    MetaLog::DefinitionPtr mml_definition;
    MetaLog::WriterPtr mml_writer;
    LoadBalancer *balancer;
    MonitoringPtr monitoring;
    ResponseManager *response_manager;
    ReferenceManager *reference_manager;
    TablePtr metadata_table;
    TablePtr rs_metrics_table;
    uint64_t range_split_size;
    time_t request_timeout;
    uint32_t timer_interval;
    uint32_t monitoring_interval;
    uint32_t gc_interval;
    time_t next_monitoring_time;
    time_t next_gc_time;
    size_t conn_count;
    bool test_mode;
    OperationProcessor *op;
    OperationBalance *op_balance;
    String location_hash;
    int32_t max_allowable_skew;
    bool in_operation;

    void add_server(RangeServerConnectionPtr &rsc);
    bool connect_server(RangeServerConnectionPtr &rsc, const String &hostname, InetAddr local_addr, InetAddr public_addr);
    bool disconnect_server(RangeServerConnectionPtr &rsc);
    void wait_for_server();

    bool find_server_by_location(const String &location, RangeServerConnectionPtr &rsc);
    bool find_server_by_hostname(const String &hostname, RangeServerConnectionPtr &rsc);
    bool find_server_by_public_addr(InetAddr addr, RangeServerConnectionPtr &rsc);
    bool find_server_by_local_addr(InetAddr addr, RangeServerConnectionPtr &rsc);
    bool next_available_server(RangeServerConnectionPtr &rsc);
    bool reassigned(TableIdentifier *table, RangeSpec &range, String &location);
    bool is_connected(const String &location);
    void get_unbalanced_servers(const std::vector<String> &locations,
        std::vector<RangeServerConnectionPtr> &unbalanced);
    void set_servers_balanced(const std::vector<RangeServerConnectionPtr> &servers);
    size_t connection_count() { ScopedLock lock(mutex); return conn_count; }
    size_t server_count() { ScopedLock lock(mutex); return m_server_list.size(); }
    void get_servers(std::vector<RangeServerConnectionPtr> &servers);
    bool can_accept_ranges(const RangeServerStatistics &stats);

  private:

    class RangeServerConnectionEntry {
    public:
      RangeServerConnectionEntry(RangeServerConnectionPtr &_rsc) : rsc(_rsc) { }
      RangeServerConnectionPtr rsc;
      String location() const { return rsc->location(); }
      String hostname() const { return rsc->hostname(); }
      InetAddr public_addr() const { return rsc->public_addr(); }
      InetAddr local_addr() const { return rsc->local_addr(); }
      bool connected() const { return rsc->connected(); }
      bool removed() const { return rsc->get_removed(); }
    };

    struct InetAddrHash {
      std::size_t operator()(InetAddr addr) const {
        return (std::size_t)(addr.sin_addr.s_addr ^ addr.sin_port);
      }
    };

    typedef boost::multi_index_container<
      RangeServerConnectionEntry,
      indexed_by<
        sequenced<>,
        hashed_unique<const_mem_fun<RangeServerConnectionEntry, String,
                                    &RangeServerConnectionEntry::location> >,
        hashed_non_unique<const_mem_fun<RangeServerConnectionEntry, String,
                                        &RangeServerConnectionEntry::hostname> >,
        hashed_unique<const_mem_fun<RangeServerConnectionEntry, InetAddr,
                                    &RangeServerConnectionEntry::public_addr>, InetAddrHash>,
        hashed_non_unique<const_mem_fun<RangeServerConnectionEntry, InetAddr,
                                        &RangeServerConnectionEntry::local_addr>, InetAddrHash>
        >
      > ServerList;

    typedef ServerList::nth_index<0>::type Sequence;
    typedef ServerList::nth_index<1>::type LocationIndex;
    typedef ServerList::nth_index<2>::type HostnameIndex;
    typedef ServerList::nth_index<3>::type PublicAddrIndex;
    typedef ServerList::nth_index<4>::type LocalAddrIndex;

    ServerList m_server_list;
    ServerList::iterator m_server_list_iter;
  };
  typedef intrusive_ptr<Context> ContextPtr;

} // namespace Hypertable

#endif // HYPERTABLE_CONTEXT_H
