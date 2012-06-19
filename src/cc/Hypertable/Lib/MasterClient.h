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

#ifndef HYPERTABLE_MASTERCLIENT_H
#define HYPERTABLE_MASTERCLIENT_H

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include "AsyncComm/ApplicationQueue.h"
#include "AsyncComm/CommBuf.h"
#include "AsyncComm/ConnectionManager.h"
#include "AsyncComm/DispatchHandler.h"

#include "Common/ReferenceCount.h"
#include "Common/StatsSystem.h"
#include "Common/Timer.h"

#include "Hyperspace/HandleCallback.h"
#include "Hyperspace/Session.h"

#include "Hypertable/Lib/BalancePlan.h"
#include "Hypertable/Lib/Types.h"


namespace Hypertable {

  class Comm;
  class MasterClient;
  class MasterClientHyperspaceSessionCallback: public Hyperspace::SessionCallback {
  public:
    MasterClientHyperspaceSessionCallback() {}
    ~MasterClientHyperspaceSessionCallback() {}
    void safe() {}
    void expired() {}
    void jeopardy() {}
    void disconnected();
    void reconnected();

  private:
    friend class MasterClient;
    MasterClient *m_masterclient;
  };

  class MasterClient : public ReferenceCount {
  public:

    MasterClient(ConnectionManagerPtr &, Hyperspace::SessionPtr &,
                 const String &toplevel_dir, uint32_t timeout_ms,
                 ApplicationQueuePtr &);
    MasterClient(ConnectionManagerPtr &, InetAddr &addr, uint32_t timeout_ms);
    MasterClient(Comm *comm, InetAddr &addr, uint32_t timeout_ms);
    ~MasterClient();

    void initiate_connection(DispatchHandlerPtr dhp=0, ConnectionInitializerPtr init=0);

    bool wait_for_connection(uint32_t max_wait_ms);
    bool wait_for_connection(Timer &timer);

    void create_namespace(const String &name, int flags,
                          DispatchHandler *handler, Timer *timer=0);
    void create_namespace(const String &name, int flags, Timer *timer=0);

    void drop_namespace(const String &name, bool if_exists,
                        DispatchHandler *handler, Timer *timer=0);
    void drop_namespace(const String &name, bool if_exists, Timer *timer=0);

    void create_table(const String &tablename, const String &schema,
                      DispatchHandler *handler, Timer *timer = 0);
    void create_table(const String &tablename, const String &schema,
                      Timer *timer = 0);
    void alter_table(const String &tablename, const String &schema,
                     DispatchHandler *handler, Timer *timer = 0);
    void alter_table(const String &tablename, const String &schema,
                     Timer *timer = 0);
    void rename_table(const String &old_name, const String &new_name,
                      DispatchHandler *handler, Timer *timer = 0);
    void rename_table(const String &old_name, const String &new_name,
                      Timer *timer = 0);
    void get_schema(const String &tablename, DispatchHandler *handler,
                    Timer *timer = 0);
    void get_schema(const String &tablename, std::string &schema, Timer *timer=0);

    void status(Timer *timer=0);

    void move_range(const String &source, TableIdentifier *table,
		    RangeSpec &range, const String &log_dir,
		    uint64_t soft_limit, bool split,
		    DispatchHandler *handler, Timer *timer = 0);
    void move_range(const String &source, TableIdentifier *table,
		    RangeSpec &range, const String &log_dir,
		    uint64_t soft_limit, bool split, Timer *timer=0);

    void relinquish_acknowledge(const String &source, TableIdentifier *table, RangeSpec &range,
                                DispatchHandler *handler, Timer *timer = 0);
    void relinquish_acknowledge(const String &source, TableIdentifier *table, RangeSpec &range,
                                Timer *timer=0);

    void drop_table(const String &table_name, bool if_exists,
                    DispatchHandler *handler, Timer *timer=0);
    void drop_table(const String &table_name, bool if_exists, Timer *timer=0);

    void shutdown(Timer *timer=0);

    void balance(BalancePlan &plan, DispatchHandler *handler,
                 Timer *timer = 0);

    void balance(BalancePlan &plan, Timer *timer = 0);

    void stop(const String &rsname, bool recover, Timer *timer = 0);

    void reload_master();

    void set_verbose_flag(bool verbose) { m_verbose = verbose; }

  private:
    friend class MasterClientHyperspaceSessionCallback;

    void hyperspace_disconnected();
    void hyperspace_reconnected();
    void send_message_async(CommBufPtr &cbp, DispatchHandler *handler, Timer *timer, const String &label);
    bool send_message(CommBufPtr &cbp, Timer *timer, EventPtr &event, const String &label);
    void fetch_result(int64_t id, Timer *timer, EventPtr &event, const String &label);
    void initialize_hyperspace();
    void initialize(Timer *&timer, Timer &tmp_timer);

    Mutex                  m_mutex;
    boost::condition       m_cond;
    bool                   m_verbose;
    Comm                  *m_comm;
    ConnectionManagerPtr   m_conn_manager;
    Hyperspace::SessionPtr m_hyperspace;
    ApplicationQueuePtr    m_app_queue;
    uint64_t               m_master_file_handle;
    Hyperspace::HandleCallbackPtr m_master_file_callback;
    InetAddr               m_master_addr;
    String                 m_master_addr_string;
    DispatchHandlerPtr     m_dispatcher_handler;
    ConnectionInitializerPtr m_connection_initializer;
    bool                   m_hyperspace_init;
    bool                   m_hyperspace_connected;
    Mutex                  m_hyperspace_mutex;
    uint32_t               m_timeout_ms;
    MasterClientHyperspaceSessionCallback m_hyperspace_session_callback;
    String                 m_toplevel_dir;
    uint32_t               m_retry_interval;
  };

  typedef intrusive_ptr<MasterClient> MasterClientPtr;


}

#endif // HYPERTABLE_MASTERCLIENT_H
