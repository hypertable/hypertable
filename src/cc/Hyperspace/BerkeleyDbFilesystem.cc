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

#include <Hyperspace/BerkeleyDbFilesystem.h>
#include <Hyperspace/DbtManaged.h>

#include <Common/Logger.h>
#include <Common/Path.h>
#include <Common/ScopeGuard.h>
#include <Common/Serialization.h>
#include <Common/String.h>
#include <Common/System.h>
#include <Common/SystemInfo.h>
#include <Common/Time.h>

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

#include <cctype>
#include <cstdlib>
#include <ostream>
#include <sstream>
#include <vector>

using namespace boost::algorithm;
using namespace Hyperspace;
using namespace Hypertable;
using namespace Error;
using namespace StateDbKeys;

#define HT_DEBUG_ATTR(_txn_, _fn_, _an_, _k_, _v_) \
  HT_DEBUG_OUT <<"txn="<< (_txn_) <<" fname='"<< (_fn_) <<"' attr='"<< (_an_) \
      <<"' key='"<< (char *)(_k_).get_data() <<"' value='"<< (_v_) <<"'" \
      << HT_END

#define HT_DEBUG_ATTR_(_txn_, _fn_, _an_, _k_, _v_, _l_) \
  HT_DEBUG_OUT <<"txn="<< (_txn_) <<" fname='"<< (_fn_); \
  if ((_an_) == String()) _out_ <<"' attr='"<< (_an_); \
  _out_ <<"' key='" << (char *)(_k_).get_data(); \
  if (_l_) _out_ <<"' value='"<< format_bytes(20, _v_, _l_); \
  _out_ <<"'"<< HT_END

void close_db_cursor(Dbc **cursor) {
  if (*cursor != 0) {
    (*cursor)->close();
    *cursor = 0;
  }
}

const char* BerkeleyDbFilesystem::ms_name_namespace_db = "namespace.db";
const char* BerkeleyDbFilesystem::ms_name_state_db = "state.db";

BerkeleyDbFilesystem::BerkeleyDbFilesystem(PropertiesPtr &props,
                                           const std::string &basedir,
                                           const std::vector<Thread::id> &thread_ids,
                                           bool force_recover)
    : m_base_dir(basedir), m_env(0) {

  m_checkpoint_size_kb = props->get_i32("Hyperspace.Checkpoint.Size") / 1000;
  m_max_unused_logs = props->get_i32("Hyperspace.LogGc.MaxUnusedLogs");
  m_log_gc_interval = std::chrono::milliseconds(props->get_i32("Hyperspace.LogGc.Interval"));
  m_last_log_gc_time = std::chrono::steady_clock::now();

  u_int32_t env_flags =
    DB_CREATE |      // If the environment does not exist, create it
    DB_INIT_LOCK  |  // Initialize locking
    DB_INIT_LOG   |  // Initialize logging
    DB_INIT_MPOOL |  // Initialize the cache
    DB_INIT_TXN   |  // Initialize transactions
    DB_INIT_REP   |  // Initialize replication
    DB_RECOVER    |  // Do basic recovery
    DB_THREAD;

  if (force_recover)
    env_flags |= DB_RECOVER_FATAL;

   m_db_flags = DB_CREATE | DB_AUTO_COMMIT | DB_THREAD;

  /*
   * Open Berkeley DB environment and namespace database
   */
  try {
    int ret;
    Dbt key, data;
    DbtManaged keym, datam;
    char numbuf[17];
    String localhost = System::net_info().host_name;
    String localip = System::net_info().primary_addr;
    HT_INFOF("localhost=%s localip=%s", localhost.c_str(), localip.c_str());

    m_env.set_lk_detect(DB_LOCK_DEFAULT);
    m_env.set_app_private(&m_replication_info);
    m_env.set_event_notify(db_event_callback);
    m_env.set_msgcall(db_msg_callback);
    m_env.set_errcall(db_err_callback);
    m_env.set_verbose(DB_VERB_REPLICATION, 1);
    m_env.open(m_base_dir.c_str(), env_flags, 0);
    {
      // Haven't implemented state recovery yet, so delete the old statedb and create the new one
      String state_db = m_base_dir + "/" + ms_name_state_db;
      HT_DEBUG_OUT << "Check for  & remove for existing statedb=" << state_db << HT_END;
      if (FileUtils::exists(state_db)) {
        HT_INFO("Removing statedb");
        Db old_state_db(&m_env, 0);
        old_state_db.remove(ms_name_state_db, NULL, 0);
      }
    }

    /*
     * Setup replication
     * TODO: add condition to replication info object to keep track of whether local
     * replica is a master or not. If it is the master then setup changes that the master
     * environment needs to make.
     */
    if (props->has("Hyperspace.Replica.Host"))
      m_replication_info.num_replicas = props->get_strs("Hyperspace.Replica.Host").size();

    if (m_replication_info.num_replicas > 1) {

      int replication_port = props->get_i16("Hyperspace.Replica.Replication.Port");
      // just look at hostname
      size_t localhost_len = localhost.find('.');
      if (localhost_len == std::string::npos)
        localhost_len = localhost.length();
      localhost = localhost.substr(0, localhost_len);

      // set master leases
      m_env.rep_set_config(DB_REP_CONF_LEASE, 1);
      // send writes that are part of one transaction in a single network xfer
      m_env.rep_set_config(DB_REP_CONF_BULK, 1);
      // in case there are only 2 replication sites, client can't become master in case
      // master fails
      m_env.rep_set_config(DB_REPMGR_CONF_2SITE_STRICT, 1);
      // all replicas must ack all txns
      m_env.repmgr_set_ack_policy(DB_REPMGR_ACKS_QUORUM);

#if !(DB_VERSION_MAJOR > 5 || (DB_VERSION_MAJOR == 5 && DB_VERSION_MINOR >= 2))
      m_env.rep_set_nsites(m_replication_info.num_replicas);
#endif

      // BDB times are in microseconds
      m_env.rep_set_timeout(DB_REP_HEARTBEAT_SEND, 30000000); //30s
      m_env.rep_set_timeout(DB_REP_HEARTBEAT_MONITOR, 60000000); //60s
      m_env.rep_set_timeout(DB_REP_ACK_TIMEOUT,
                            props->get_i32("Hyperspace.Replica.Replication.Timeout")*1000);
      m_env.rep_set_timeout(DB_REP_CONNECTION_RETRY, 5000000); //5s
      m_env.rep_set_timeout(DB_REP_LEASE_TIMEOUT,
                            props->get_i32("Hyperspace.Replica.Replication.Timeout")*1000);

      int priority = m_replication_info.num_replicas;
      for (auto replica : props->get_strs("Hyperspace.Replica.Host")) {
        bool is_ipv4 = InetAddr::is_ipv4(replica.c_str());
        bool is_localhost=false;
        Endpoint e;

        if (is_ipv4) {
          e = InetAddr::parse_endpoint(replica, replication_port);
          if (replica == localip)
            is_localhost = true;
        }
        else {
          size_t replica_len = replica.find('.');
          if (replica_len == std::string::npos)
            replica_len = replica.length();
          replica = replica.substr(0, replica_len);
          e = InetAddr::parse_endpoint(replica, replication_port);
          if (boost::iequals(localhost, e.host))
            is_localhost = true;
        }
        if (is_localhost) {
          // make sure all replicas are electable and have same priority
          m_env.rep_set_priority(priority);

#if DB_VERSION_MAJOR > 5 || (DB_VERSION_MAJOR == 5 && DB_VERSION_MINOR >= 2)
          DbSite *dbsite;
          m_env.repmgr_site(e.host.c_str(), e.port, &dbsite, 0);
          dbsite->set_config(DB_LOCAL_SITE, 1);
          dbsite->set_config(DB_LEGACY, 1);
          dbsite->close();
#else
          m_env.repmgr_set_local_site(e.host.c_str(), e.port, 0);
#endif
          m_replication_info.localhost = e.host;
          HT_INFOF("Added local replication site %s priority=%d", e.host.c_str(), priority);
        }
        else {
          int eid;

#if DB_VERSION_MAJOR > 5 || (DB_VERSION_MAJOR == 5 && DB_VERSION_MINOR >= 2)
          DbSite *dbsite;
          m_env.repmgr_site(e.host.c_str(), e.port, &dbsite, 0);
          dbsite->set_config(DB_BOOTSTRAP_HELPER, 1);
          dbsite->set_config(DB_LEGACY, 1);
          dbsite->get_eid(&eid);
          dbsite->close();
#else
          m_env.repmgr_add_remote_site(e.host.c_str(), e.port, &eid, 0);
#endif
          m_replication_info.replica_map[eid] = e.host;
          HT_INFOF("Added remote replication site %s priority=%d", e.host.c_str(), priority);
        }
        --priority;
      }
      m_env.repmgr_start(3, DB_REP_ELECTION);
      m_replication_info.do_replication = true;
      m_replication_info.wait_for_election();
    }
    else {
      m_replication_info.do_replication = false;
    }


    // only master can initiate writes
    if (is_master()) {
      // do checkpoint
      m_env.txn_checkpoint(0, 0, 0);

      // open handles
      Db *handle_namespace_db = new Db(&m_env, 0);
      Db *handle_state_db     = new Db(&m_env, 0);

      try {
        handle_namespace_db->open(NULL, ms_name_namespace_db, NULL, DB_BTREE, m_db_flags, 0);
      }
      catch(DbException &e) {
        // retry if locker killed to resolve a deadlock
        if (e.get_errno() == DB_LOCK_DEADLOCK)
          handle_namespace_db->open(NULL, ms_name_namespace_db, NULL, DB_BTREE, m_db_flags, 0);
        else
          throw;
      }

      handle_state_db->set_flags(DB_DUP|DB_REVSPLITOFF);
      try {
        handle_state_db->open(NULL, ms_name_state_db, NULL, DB_BTREE, m_db_flags, 0);
      }
      catch(DbException &e) {
        // retry if locker killed to resolve a deadlock
        if (e.get_errno() == DB_LOCK_DEADLOCK)
          handle_state_db->open(NULL, ms_name_state_db, NULL, DB_BTREE, m_db_flags, 0);
        else
          throw;
      }

      key.set_data((void *)"/");
      key.set_size(2);

      data.set_flags(DB_DBT_REALLOC);
      if ((ret = handle_namespace_db->get(NULL, &key, &data, 0)) == DB_NOTFOUND) {
        data.set_data(0);
        data.set_size(0);
        ret = handle_namespace_db->put(NULL, &key, &data, 0);
        key.set_data((void *)"/hyperspace/");
        key.set_size(strlen("/hyperspace/")+1);
        ret = handle_namespace_db->put(NULL, &key, &data, 0);
        key.set_data((void *)"/hyperspace/metadata");
        key.set_size(strlen("/hyperspace/metadata")+1);
        ret = handle_namespace_db->put(NULL, &key, &data, 0);
      }

      if (data.get_data() != 0)
        free(data.get_data());

      // initialize statedb if reqd
      key.set_data((void *)"/");
      key.set_size(2);

      data.set_flags(DB_DBT_REALLOC);
      data.set_data(0);
      data.set_size(0);

      if((ret = handle_state_db->get(NULL, &key, &data, 0)) == DB_NOTFOUND) {
        data.set_data(0);
        data.set_size(0);
        ret = handle_state_db->put(NULL, &key, &data, 0);
        HT_ASSERT(ret == 0);
      }
      // init next ids in statedb
      keym.set_str(NEXT_SESSION_ID);
      if ((ret = handle_state_db->get(NULL, &keym, &datam, 0)) == DB_NOTFOUND) {
        sprintf(numbuf, "%llu", (Llu)1);
        datam.set_str(numbuf);
        ret = handle_state_db->put(NULL, &keym, &datam, 0);
        HT_ASSERT(ret==0);
      }
      keym.set_str(NEXT_HANDLE_ID);
      if ( (ret = handle_state_db->get(NULL, &keym, &datam, 0)) == DB_NOTFOUND) {
        sprintf(numbuf, "%llu", (Llu)1);
        datam.set_str(numbuf);
        ret = handle_state_db->put(NULL, &keym, &datam, 0);
        HT_ASSERT(ret==0);
      }
      keym.set_str(NEXT_EVENT_ID);
      if ((ret = handle_state_db->get(NULL, &keym, &datam, 0)) == DB_NOTFOUND) {
        sprintf(numbuf, "%llu", (Llu)1);
        datam.set_str(numbuf);
        ret = handle_state_db->put(NULL, &keym, &datam, 0);
        HT_ASSERT(ret==0);
      }

      if (data.get_data() != 0)
        free(data.get_data());

      //close handles
      handle_state_db->close(0);
      delete handle_state_db;
      handle_state_db = 0;
      handle_namespace_db->close(0);
      delete handle_namespace_db;
      handle_namespace_db = 0;
      HT_INFO("Replication master init done");
    }

  }
  catch (DbException &e) {
    unsigned count = 0;
    DB_REPMGR_SITE *list = 0;
    if (m_env.repmgr_site_list(&count, &list) == 0) {
      String msg;
      if (count)
        msg = String("The following replicas are (dis)connected: ");
      for (unsigned i = 0; i < count; i++) {
        const char *status = "unknown";
        switch (list[i].status) {
          case DB_REPMGR_CONNECTED:
            status = "connected";
            break;
          case DB_REPMGR_DISCONNECTED:
            status = "disconnected";
            break;
        }
        msg += String("(") + list[i].host + " status: " + status;
      }
      if (count)
        HT_INFOF("%s", msg.c_str());
    }
    HT_FATALF("Error initializing Berkeley DB (dir=%s) - %s; please make sure "
              "that the hyperspace replicas are correctly configured.",
               m_base_dir.c_str(), e.what());
  }

  // initialize per thread DB handles
  init_db_handles(thread_ids);
  HT_DEBUG_OUT <<"namespace initialized"<< HT_END;
}

/*
 */
BerkeleyDbFilesystem::~BerkeleyDbFilesystem() {
  /*
   * Close Berkeley DB "namespace" database and environment
   */
  try {
    HT_INFO("Closed DB handles for all threads ");
    for (auto &val : m_thread_handle_map) {
      (val.second)->close();
    }
    m_env.close(0);
  }
  catch(DbException &e) {
    HT_ERRORF("Error closing Berkeley DB (dir=%s) - %s",
              m_base_dir.c_str(), e.what());
  }
  HT_INFO("namespace closed");
}

void BerkeleyDbFilesystem::db_msg_callback(const DbEnv *dbenv, const char *msg)
{
  HT_DEBUG_OUT << "BDB MESSAGE:" << msg << HT_END;
}

void BerkeleyDbFilesystem::db_msg_callback(const DbEnv *dbenv, const char *msgpfx, 
                                           const char *msg)
{
  HT_DEBUG_OUT << "BDB MESSAGE:" << msg << HT_END;
}


void BerkeleyDbFilesystem::db_err_callback(const DbEnv *dbenv, const char *errpfx,
                                           const char *msg)
{
  HT_INFOF("BDB ERROR: %s", msg);
}


void BerkeleyDbFilesystem::db_event_callback(DbEnv *dbenv, uint32_t which, void *info)
{
  ReplicationInfo *replication_info = (ReplicationInfo*)dbenv->get_app_private();

  switch (which) {
  case DB_EVENT_REP_CLIENT:
    HT_INFO("Received DB_EVENT_REP_CLIENT event");
   break;
  case DB_EVENT_REP_MASTER:
    HT_INFO("Received DB_EVENT_REP_MASTER event");
    HT_INFOF("Local site elected master: %s", replication_info->localhost.c_str());
   replication_info->is_master = true;
   replication_info->finish_election();
   break;
  case DB_EVENT_REP_ELECTED:
    HT_INFO("Received DB_EVENT_REP_ELECTED event ignore and wait for DB_EVENT_REP_MASTER");
    break;
  case DB_EVENT_REP_NEWMASTER:
    HT_INFO("Received DB_EVENT_REP_NEWMASTER event");
   // exit if we lost mastership
   if (replication_info->is_master)
     HT_FATAL("Local site lost mastership");
   replication_info->master_eid = *((int*)info);
   {
     auto it = replication_info->replica_map.find(replication_info->master_eid);
     HT_ASSERT (it != replication_info->replica_map.end());
     HT_INFOF("New master elected: %s", it->second.c_str());

     if (replication_info->election_finished())
       HT_FATAL("New master elected after initial master election.");

     replication_info->finish_election();
   }
   break;
  case DB_EVENT_REP_PERM_FAILED:
   if (replication_info->is_master)
     HT_FATAL("Replication failed. Master did not receive enough acks.");
   break;
  case DB_EVENT_PANIC:
   HT_FATAL("Received DB_EVENT_PANIC event");
   break;
  case DB_EVENT_REP_STARTUPDONE:
    HT_INFO("Received DB_EVENT_REP_STARTUPDONE event");
   break;
  case DB_EVENT_WRITE_FAILED:
    HT_INFO("Received DB_EVENT_WROTE_FAILED event");
   break;

#if DB_VERSION_MAJOR > 5 || (DB_VERSION_MAJOR == 5 && DB_VERSION_MINOR >= 2)
  case DB_EVENT_REP_CONNECT_BROKEN:
    HT_INFO("Received DB_EVENT_REP_CONNECT_BROKEN event");
   break;
  case DB_EVENT_REP_CONNECT_ESTD:
    HT_INFO("Received DB_EVENT_REP_CONNECT_ESTD event");
   break;
  case DB_EVENT_REP_CONNECT_TRY_FAILED:
    HT_INFO("Received DB_EVENT_REP_CONNECT_TRY_FAILED event");
   break;
  case DB_EVENT_REP_DUPMASTER:
    HT_INFO("Received DB_EVENT_REP_DUPMASTER event");
   break;
  case DB_EVENT_REP_ELECTION_FAILED:
    HT_INFO("Received DB_EVENT_REP_ELECTION_FAILED event");
   break;
  case DB_EVENT_REP_INIT_DONE:
    HT_INFO("Received DB_EVENT_REP_INIT_DONE event");
   break;
  case DB_EVENT_REP_JOIN_FAILURE:
    HT_INFO("Received DB_EVENT_REP_JOIN_FAILURE event");
   break;
  case DB_EVENT_REP_LOCAL_SITE_REMOVED:
    HT_INFO("Received DB_EVENT_REP_LOCAL_SITE_REMOVED event");
   break;
  case DB_EVENT_REP_MASTER_FAILURE:
    HT_INFO("Received DB_EVENT_REP_MASTER_FAILURE event");
   break;
  case DB_EVENT_REP_SITE_ADDED:
    HT_INFO("Received DB_EVENT_REP_SITE_ADDED event");
   break;
  case DB_EVENT_REP_SITE_REMOVED:
    HT_INFO("Received DB_EVENT_REP_SITE_REMOVED event");
   break;
#endif

  default:
    HT_INFO("Received BerkeleyDB event ");
  }
}

void BerkeleyDbFilesystem::init_db_handles(const std::vector<Thread::id> &thread_ids) {

  BDbHandlesPtr db_handles;

  // Assign per thread handles but don't open them yet
  std::stringstream tid_str;
  for (auto thread_id : thread_ids) {
    db_handles = std::make_shared<BDbHandles>();
    m_thread_handle_map[thread_id] = db_handles;
    tid_str << thread_id;
    HT_INFOF("Created DB handles for thread: %s", tid_str.str().c_str());
    tid_str.str(std::string());
  }

}


BDbHandlesPtr BerkeleyDbFilesystem::get_db_handles() {
  ThreadHandleMap::iterator it = m_thread_handle_map.find(ThisThread::get_id());
  if (it == m_thread_handle_map.end())
    HT_FATAL_OUT << "No thread handle found for thread " << ThisThread::get_id() << HT_END;

  // Open per thread handles if not already open
  if (!it->second->open) {
    it->second->handle_namespace_db = new Db(&m_env, 0);
    it->second->handle_state_db = new Db(&m_env, 0);
    it->second->handle_namespace_db->open(NULL, ms_name_namespace_db,
                                            NULL, DB_BTREE, m_db_flags, 0);
    it->second->handle_state_db->set_flags(DB_DUP|DB_REVSPLITOFF);
    it->second->handle_state_db->open(NULL, ms_name_state_db, NULL,
                                        DB_BTREE, m_db_flags, 0);
    it->second->open=true;
  }
  return it->second;

}

void BerkeleyDbFilesystem::do_checkpoint() {

  // do checkpoint, don't bother to check if this is the master
  // since its just ignored be  slaves
  HT_DEBUG_OUT << "Do checkpoint if log > " << m_checkpoint_size_kb << "KB" << HT_END;
  int ret;
  try {
    ret = m_env.txn_checkpoint(m_checkpoint_size_kb, 0, 0);
    if (ret != 0) {
      HT_FATAL_OUT << "Unable to do checkpoint got ret=" << ret << HT_END;
    }
  }
  catch (DbException &e) {
    HT_FATAL_OUT << "Error checkpointing BerkeleyDb: " << e.what() << HT_END;
  }

  auto now = std::chrono::steady_clock::now();
  auto time_elapsed = now - m_last_log_gc_time;

  if (time_elapsed > m_log_gc_interval) {
    m_last_log_gc_time = now;

    // delete all but the last max_unused_logs files
    char **unused_logs, **log;
    uint32_t unused_logs_count=0;
    unused_logs = log = NULL;

    try {
      ret = m_env.log_archive(&unused_logs, DB_ARCH_ABS);
      if (ret != 0) {
        HT_FATAL_OUT << "Unable to get list of unused BerkeleyDB log files, got ret=" << ret
                   << HT_END;
      }
    }
    catch (DbException &e) {
      HT_FATAL_OUT<< "Error getting list of unused BerkeleyDb log files: "
                  << e.what() << HT_END;
    }


    if (unused_logs != NULL) {
      for (log = unused_logs; *log != NULL; ++log)
        unused_logs_count++;

      for (log = unused_logs; *log != NULL && unused_logs_count > m_max_unused_logs;
           ++log, --unused_logs_count) {
        // delete log file
        Path file(*log);
        boost::filesystem::remove(file);
        HT_INFOF("Deleted unused BerkeleyDb log %s", *log);
      }
      free(unused_logs);
    }
  }

}

void BerkeleyDbFilesystem::start_transaction(BDbTxn &txn) {

  // begin transaction
  try {
    HT_ASSERT(txn.handle_namespace_db == 0 && txn.handle_state_db == 0);

    // open db handles
    BDbHandlesPtr db_handles = get_db_handles();

    // Use handles for this thread
    txn.handle_namespace_db = db_handles->handle_namespace_db;
    txn.handle_state_db = db_handles->handle_state_db;

    // open txn
    m_env.txn_begin(NULL, &txn.db_txn, 0);
  }
  catch (DbException &e) {
    // issue 915: a failure of txn_begin is possible if BDB ran out of
    // locker entries
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"txn="<< txn << HT_END;

  return;
}


/*
 */
bool
BerkeleyDbFilesystem::get_xattr_i32(BDbTxn &txn, const String &fname,
    const String &aname, uint32_t *valuep) {
  int ret;
  Dbt key;
  DbtManaged data;
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);

  try {
    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) == 0) {
      *valuep = strtoll((const char *)data.get_data(), 0, 0);
      HT_DEBUG_ATTR(txn, fname, aname, key, *valuep);
      return true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  return false;
}



/*
 */
void
BerkeleyDbFilesystem::set_xattr_i32(BDbTxn &txn, const String &fname,
                                    const String &aname, uint32_t value) {
  int ret;
  Dbt key, data;
  char numbuf[16];
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);

  sprintf(numbuf, "%u", value);

  data.set_data(numbuf);
  data.set_size(strlen(numbuf)+1);

  try {
    ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0);
    HT_DEBUG_ATTR(txn, fname, aname, key, value);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_ASSERT(ret == 0);
}


/*
 */
bool
BerkeleyDbFilesystem::get_xattr_i64(BDbTxn &txn, const String &fname,
    const String &aname, uint64_t *valuep) {
  int ret;
  Dbt key;
  DbtManaged data;
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);

  try {
    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) == 0) {
      *valuep = strtoll((const char *)data.get_data(), 0, 0);
      HT_DEBUG_ATTR(txn, fname, aname, key, *valuep);
      return true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  return false;
}



/*
 */
void
BerkeleyDbFilesystem::set_xattr_i64(BDbTxn &txn, const String &fname,
                                    const String &aname, uint64_t value) {
  int ret;
  Dbt key, data;
  char numbuf[24];
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);

  sprintf(numbuf, "%llu", (Llu)value);

  data.set_data(numbuf);
  data.set_size(strlen(numbuf)+1);

  try {
    ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0);
    HT_DEBUG_ATTR(txn, fname, aname, key, value);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_ASSERT(ret == 0);
}

/*
 */
bool
BerkeleyDbFilesystem::incr_attr(BDbTxn &txn, const String &fname, const String &aname,
                                uint64_t *valuep) {
  int ret;
  Dbt key, data;
  String keystr = fname;
  char numbuf[24];
  uint64_t new_value;

  build_attr_key(txn, keystr, aname, key);

  try {
    data.set_flags(DB_DBT_REALLOC);
    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) == 0) {
      if (data.get_size() >= 24)
        HT_THROWF(HYPERSPACE_BAD_ATTRIBUTE,
                  "incr attribute '%s' exceeds 24 characters", aname.c_str());

      memcpy(numbuf, (const char *)data.get_data(), data.get_size());
      numbuf[data.get_size()] = 0;  // NUL-terminate

      // Sanity check value
      for (const char *ptr=numbuf; *ptr; ptr++) {
        if (!::isdigit(*ptr))
        HT_THROWF(HYPERSPACE_BAD_ATTRIBUTE,
                  "incr attribute '%s' contains invalid characters: %s",
                  aname.c_str(), numbuf);
      }

      *valuep = strtoull(numbuf, 0, 0);

      // Sanity check value
      if (*valuep == 0 && errno == EINVAL)
        HT_THROWF(HYPERSPACE_BAD_ATTRIBUTE,
                  "incr attr '%s' invalid: '%s', cannot convert to integer",
                  aname.c_str(), numbuf);

      HT_DEBUG_ATTR(txn, fname, aname, key, *valuep);
      new_value = *valuep + 1;
      sprintf(numbuf, "%llu", (Llu)new_value);
      data.set_data(numbuf);
      data.set_size(strlen(numbuf));

      if ((ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0)) == 0) {
        HT_DEBUG_ATTR(txn, fname, aname, key, new_value);
        return true;
      }
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  return false;
}
/*
 */
void
BerkeleyDbFilesystem::set_xattr(BDbTxn &txn, const String &fname,
    const String &aname, const void *value, size_t value_len) {
  int ret;
  Dbt key, data;
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);
  data.set_data((void *)value);
  data.set_size(value_len);

  try {
    HT_DEBUG_ATTR_(txn, fname, aname, key, value, value_len);
    ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_ASSERT(ret == 0);
}


/*
 */
bool
BerkeleyDbFilesystem::get_xattr(BDbTxn &txn, const String &fname,
                                const String &aname, DynamicBuffer &vbuf) {
  int ret;
  Dbt key;
  DbtManaged data;
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);

  HT_DEBUG_OUT << "get_xattr txn="<< txn <<", fname=" << fname << ", attr='" << aname << HT_END;

  try {
    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) == 0) {
      vbuf.reserve(data.get_size());
      memcpy(vbuf.base, (uint8_t *)data.get_data(), data.get_size());
      vbuf.ptr += data.get_size();
      HT_DEBUG_ATTR_(txn, fname, aname, key, vbuf.base, data.get_size());
      return true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  return false;
}

/*
 */
bool
BerkeleyDbFilesystem::exists_xattr(BDbTxn &txn, const String &fname, const String &aname)
{
  int ret;
  Dbt key;
  String keystr = fname;
  bool exists = false;

  build_attr_key(txn, keystr, aname, key);

  try {
    ret = txn.handle_namespace_db->exists(txn.db_txn, &key, 0);
    HT_EXPECT(ret == 0 || ret == DB_NOTFOUND, HYPERSPACE_BERKELEYDB_ERROR);

    if (ret == 0) {
      exists = true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  return exists;
}


void
BerkeleyDbFilesystem::del_xattr(BDbTxn &txn, const String &fname,
                                const String &aname) {
  int ret;
  Dbt key;
  String keystr = fname;

  build_attr_key(txn, keystr, aname, key);

  try {
    if ((ret = txn.handle_namespace_db->del(txn.db_txn, &key, 0)) == DB_NOTFOUND)
      HT_THROW(HYPERSPACE_ATTR_NOT_FOUND, aname);
    HT_DEBUG_ATTR_(txn, fname, aname, key, "", 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}


/*
 */
void BerkeleyDbFilesystem::mkdir(BDbTxn &txn, const String &name) {
  int ret;
  Dbt key;
  DbtManaged data;
  size_t lastslash = name.rfind('/', name.length()-1);
  String dirname = name.substr(0, lastslash+1);

  try {
    /*
     * Make sure parent directory exists
     */
    key.set_data((void *)dirname.c_str());
    key.set_size(dirname.length()+1);

    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) == DB_NOTFOUND)
      HT_THROW(HYPERSPACE_FILE_NOT_FOUND, dirname);

    // formulate directory name
    dirname = name;
    if (dirname[dirname.length()-1] != '/')
      dirname += "/";

    HT_DEBUG_OUT <<"dirname='"<< dirname <<"'"<< HT_END;

    /*
     * Make sure directory does not already exists
     */
    key.set_data((void *)dirname.c_str());
    key.set_size(dirname.length()+1);

    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) != DB_NOTFOUND)
      HT_THROW(HYPERSPACE_FILE_EXISTS, dirname);

    /*
     * Create directory entry
     */
    key.set_data((void *)dirname.c_str());
    key.set_size(dirname.length()+1);

    data.clear();

    ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0);

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}


void BerkeleyDbFilesystem::unlink(BDbTxn &txn, const String &name) {
  std::vector<String> delkeys;
  DbtManaged keym, datam;
  Dbt key;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool looks_like_dir = false;
  bool looks_like_file = false;
  String str;

  try {
    txn.handle_namespace_db->cursor(txn.db_txn, &cursorp, 0);

    keym.set_str(name);

    if (cursorp->get(&keym, &datam, DB_SET_RANGE) != DB_NOTFOUND) {
      do {
        str = keym.get_str();

        if (str.length() > name.length()) {
          if (str[name.length()] == '/') {
            if (str.length() > name.length() + 1
                && str[name.length() + 1] != NODE_ATTR_DELIM) {
              HT_THROW(HYPERSPACE_DIR_NOT_EMPTY, name);
            }
            looks_like_dir = true;
            delkeys.push_back(keym.get_str());
          }
          else if (str[name.length()] == NODE_ATTR_DELIM) {
            looks_like_file = true;
            delkeys.push_back(keym.get_str());
          }
        }
        else {
          delkeys.push_back(keym.get_str());
          looks_like_file = true;
        }

      } while (cursorp->get(&keym, &datam, DB_NEXT) != DB_NOTFOUND &&
               starts_with(keym.get_str(), name.c_str()));
    }

    HT_ASSERT(!(looks_like_dir && looks_like_file));

    if (delkeys.empty())
      HT_THROW(HYPERSPACE_FILE_NOT_FOUND, name);

    for (size_t i=0; i<delkeys.size(); i++) {
      key.set_data((void *)delkeys[i].c_str());
      key.set_size(delkeys[i].length()+1);
      HT_ASSERT(txn.handle_namespace_db->del(txn.db_txn, &key, 0) != DB_NOTFOUND);
      HT_DEBUG_ATTR_(txn, name, "", key, "", 0);
    }
  }
  catch (DbException &e) {
    HT_ERRORF("Berkeley DB error: %s", e.what());
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    else
      HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}


bool
BerkeleyDbFilesystem::exists(BDbTxn &txn, String fname, bool *is_dir_p) {
  int ret;
  Dbt key;

  if (is_dir_p)
    *is_dir_p = false;

  key.set_data((void *)fname.c_str());
  key.set_size(fname.length()+1);

  try {
    if ((ret = txn.handle_namespace_db->exists(txn.db_txn, &key, 0)) == DB_NOTFOUND) {
      fname += "/";
      key.set_data((void *)fname.c_str());
      key.set_size(fname.length()+1);

      if ((ret = txn.handle_namespace_db->exists(txn.db_txn, &key, 0)) == DB_NOTFOUND) {
        HT_DEBUG_OUT <<"'"<< fname <<"' does NOT exist."<< HT_END;
        return false;
      }
      if (is_dir_p)
        *is_dir_p = true;
    }
  }
  catch (DbException &e) {
    HT_ERRORF("Berkeley DB error: %s", e.what());
     if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    else
      HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"'"<< fname <<"' exists."<< HT_END;
  return true;
}


/*
 *
 */
void
BerkeleyDbFilesystem::create(BDbTxn &txn, const String &fname, bool temp) {
  int ret;
  Dbt key;
  DbtManaged data;

  HT_DEBUG_OUT <<"txn="<< txn <<" fname='"<< fname <<"' temp="<< temp << HT_END;

  try {
    if (exists(txn, fname, 0))
      HT_THROW(HYPERSPACE_FILE_EXISTS, fname);

    if (ends_with(fname, "/"))
      HT_THROW(HYPERSPACE_IS_DIRECTORY, fname);

    size_t lastslash = fname.rfind('/', fname.length() - 1);
    String parent_dir = fname.substr(0, lastslash + 1);

    key.set_data((void *)parent_dir.c_str());
    key.set_size(parent_dir.length()+1);

    if ((ret = txn.handle_namespace_db->get(txn.db_txn, &key, &data, 0)) == DB_NOTFOUND)
      HT_THROW(HYPERSPACE_BAD_PATHNAME, fname);

    key.set_data((void *)fname.c_str());
    key.set_size(fname.length()+1);

    ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0);

    if (temp) {
      String temp_key = fname + NODE_ATTR_DELIM +"temp";
      key.set_data((void *)temp_key.c_str());
      key.set_size(temp_key.length()+1);
      ret = txn.handle_namespace_db->put(txn.db_txn, &key, &data, 0);
    }
  }
  catch (DbException &e) {
    HT_ERRORF("Berkeley DB error: %s", e.what());
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    else
      HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }
}


void
BerkeleyDbFilesystem::get_directory_listing(BDbTxn &txn, String fname,
                                            std::vector<DirEntry> &listing) {
  DbtManaged keym, datam;
  Dbt key;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String str, last_str;
  DirEntry entry;
  size_t offset;

  try {
    txn.handle_namespace_db->cursor(txn.db_txn, &cursorp, 0);

    if (!ends_with(fname, "/"))
      fname += "/";

    HT_DEBUG_OUT <<"txn="<< txn <<" dir='"<< fname <<"'"<< HT_END;
    keym.set_str(fname);

    if (cursorp->get(&keym, &datam, DB_SET_RANGE) != DB_NOTFOUND) {

      if (!starts_with(keym.get_str(), fname.c_str())) {
        HT_THROW(HYPERSPACE_BAD_PATHNAME, fname);
      }

      do {
        str = keym.get_str();

        if (str.length() > fname.length()) {
          if (str[fname.length()] != NODE_ATTR_DELIM ) {
            str = str.substr(fname.length());

            if ((offset = str.find('/')) != String::npos) {
              entry.name = str.substr(0, offset);

              if (entry.name != last_str) {
                entry.is_dir = true;
                listing.push_back(entry);
                last_str = entry.name;
              }
            }
            else {
              if ((offset = str.find(NODE_ATTR_DELIM)) != String::npos) {
                entry.name = str.substr(0, offset);

                if (entry.name != last_str) {
                  entry.is_dir = false;
                  listing.push_back(entry);
                  last_str = entry.name;
                }
              }
              else {
                entry.name = str;

                if (entry.name != last_str) {
                  entry.is_dir = false;
                  listing.push_back(entry);
                  last_str = entry.name;
                }
              }
            }
          }
        }
      } while (cursorp->get(&keym, &datam, DB_NEXT) != DB_NOTFOUND &&
               starts_with(keym.get_str(), fname.c_str()));

    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }
}

namespace {
  const char stop_chars[3] = { (char)BerkeleyDbFilesystem::NODE_ATTR_DELIM, '/', 0 };
}

void
BerkeleyDbFilesystem::get_directory_attr_listing(BDbTxn &txn, String fname,
                                                 const String &aname,
                                                 bool include_sub_entries,
                                                 std::vector<DirEntryAttr> &listing) {
  get_directory_attr_listing(txn, fname, aname, listing);
  if (include_sub_entries) {
    if (!ends_with(fname, "/"))
      fname += "/";
    for (auto &entry : listing)
      if (entry.is_dir)
        get_directory_attr_listing(txn, fname + entry.name, aname, true, entry.sub_entries);
  }
}

void
BerkeleyDbFilesystem::get_directory_attr_listing(BDbTxn &txn, String fname,
                                                 const String &aname,
                                                 std::vector<DirEntryAttr> &listing) {
  DbtManaged keym, datam;
  Dbt key;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String entryname, last_entryname, str, attr;
  DirEntryAttr entry;
  size_t offset;

  try {
    txn.handle_namespace_db->cursor(txn.db_txn, &cursorp, 0);

    if (!ends_with(fname, "/"))
      fname += "/";

    HT_DEBUG_OUT <<"txn="<< txn <<" dir='"<< fname <<"'"<< HT_END;
    keym.set_str(fname);

    if (cursorp->get(&keym, &datam, DB_SET_RANGE) != DB_NOTFOUND) {

      if (!starts_with(keym.get_str(), fname.c_str())) {
        HT_THROW(HYPERSPACE_BAD_PATHNAME, fname);
      }

      do {

        str = keym.get_str();
        if (str.length() > fname.length() && str[fname.length()] != NODE_ATTR_DELIM) {
          str = str.substr(fname.length());
          offset = str.find_first_of(stop_chars);
          entryname = (offset == String::npos) ? str : str.substr(0, offset);
          if (entryname != last_entryname) {
            if (last_entryname != "")
              listing.push_back(entry);
            last_entryname = entryname;
            // clear entry
            entry.name = entryname;
            entry.has_attr = false;
            entry.is_dir = false;
            entry.attr.free();
          }
          if (offset != String::npos) {
            if (str[offset] == '/') {
              entry.is_dir = true;
              if (str.length() > offset+2 && str[offset+1] == NODE_ATTR_DELIM) {
                attr = str.substr(offset+2);
                // attribute matches the one we're looking for
                if (attr == aname) {
                  DynamicBuffer buffer(datam.get_size());
                  buffer.add_unchecked(datam.get_data(), datam.get_size());
                  entry.attr = buffer;
                  entry.has_attr = true;
                }
              }
            }
            else {
              attr = str.substr(offset+1);
              if (attr == aname) {
                DynamicBuffer buffer(datam.get_size());
                buffer.add_unchecked(datam.get_data(), datam.get_size());
                entry.attr = buffer;
                entry.has_attr = true;
              }
            }
          }
        }

      } while (cursorp->get(&keym, &datam, DB_NEXT) != DB_NOTFOUND &&
               starts_with(keym.get_str(), fname.c_str()));

      if (last_entryname != "")
        listing.push_back(entry);

    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }
}

void
BerkeleyDbFilesystem::get_all_names(BDbTxn &txn,
                                    std::vector<String> &names) {
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  int ret;

  HT_DEBUG_OUT <<"txn="<< txn << HT_END;

  try {
    txn.handle_namespace_db->cursor(txn.db_txn, &cursorp, 0);

    while ((ret = cursorp->get(&keym, &datam, DB_NEXT)) == 0) {
      names.push_back(keym.get_str());
    }
  }
  catch(DbException &e) {
    if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());

    HT_FATALF("Berkeley DB error: %s", e.what());
  }
  catch(std::exception &e) {
    HT_FATALF("Berkeley DB error: %s", e.what());
  }

}

void
BerkeleyDbFilesystem::build_attr_key(BDbTxn &txn, String &keystr,
                                     const String &aname, Dbt &key) {
  bool isdir;

  if (!exists(txn, keystr, &isdir))
    HT_THROW(HYPERSPACE_FILE_NOT_FOUND, keystr);

  if (isdir)
    keystr += "/";

  keystr += NODE_ATTR_DELIM;
  keystr += aname;

  key.set_data((void *)keystr.c_str());
  key.set_size(keystr.length() + 1);
}

bool
BerkeleyDbFilesystem::list_xattr(BDbTxn &txn, const String& fname,
                                 std::vector<String> &anames)
{
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  int ret;
  bool isdir;

  if (!exists(txn, fname, &isdir))
    HT_THROW(HYPERSPACE_FILE_NOT_FOUND, fname);

  const String targetkey(fname + (isdir ? "/" : "") + NODE_ATTR_DELIM);

  HT_DEBUG_OUT << "txn=" << txn << HT_END;
  try {
    txn.handle_namespace_db->cursor(txn.db_txn, &cursorp, 0);
    while ((ret = cursorp->get(&keym, &datam, DB_NEXT)) == 0) {
      /* build a prefix of a key to match against all keys later */
      String currkey = keym.get_str();

      /* this whole key is shorter than our prefix - skip it */
      if (currkey.size() < targetkey.size())
        continue;

      size_t keysize = targetkey.size();
      if (!targetkey.compare(0, keysize, currkey, 0, keysize)) {
        /* strip everything before and including attribute
           delimiter to get the attribute name */
        String attribute(currkey.substr(keysize, currkey.size()));
        anames.push_back(attribute);
      }
    }
  } catch(DbException &e) {
    HT_FATALF("Berkeley DB error: %s", e.what());
    return false;
  } catch(std::exception &e) {
    HT_FATALF("Berkeley DB error: %s", e.what());
    return false;
  }

  return true;
}

/*
 *
 */
void
BerkeleyDbFilesystem::create_event(BDbTxn &txn, uint32_t type, uint64_t id,
    uint32_t mask)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"create_event txn="<< txn <<" event type='"<< type <<"' id="
      << id << " mask=" << mask << HT_END;
  try {

    HT_ASSERT(!(event_exists(txn, id)));

    // Store id under "/EVENTS/"
    String events_dir = EVENTS_STR;
    keym.set_str(events_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);

    // Store event type
    key_str = get_event_key(id, EVENT_TYPE);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)type);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
    // Store event mask
    key_str = get_event_key(id, EVENT_MASK);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)mask);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}

/*
 *
 */
void
BerkeleyDbFilesystem::create_event(BDbTxn &txn, uint32_t type, uint64_t id,
    uint32_t mask, const String &name)
{
  int ret;
  String key_str;
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  try {
    create_event(txn, type, id, mask);
    // Store event name
    key_str = get_event_key(id, EVENT_NAME);
    keym.set_str(key_str);
    datam.set_str(name);
    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}
/*
 *
 */
void
BerkeleyDbFilesystem::create_event(BDbTxn &txn, uint32_t type, uint64_t id,
    uint32_t mask, uint32_t mode)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  char numbuf[16];

  try {
    create_event(txn, type, id, mask);
    // Store mode
    key_str = get_event_key(id, EVENT_MODE);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)mode);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }
}
/*
 *
 */
void
BerkeleyDbFilesystem::create_event(BDbTxn &txn, uint32_t type, uint64_t id,
    uint32_t mask, uint32_t mode,
    uint64_t generation)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[16];

  try {
    create_event(txn, type, id, mask, mode);
    // Store generation
    key_str = get_event_key(id, EVENT_GENERATION);
    keym.set_str(key_str);
    sprintf(numbuf, "%llu", (Llu)generation);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }
}

/*
 *
 */
void
BerkeleyDbFilesystem::set_event_notification_handles(BDbTxn &txn, uint64_t id,
    const std::vector<uint64_t> &handles)
{
  int ret;
  DbtManaged  keym;
  String key_str;
  // 4 bytes for #handles, 8 bytes/handle
  uint32_t serialized_len = handles.size() * (sizeof(uint64_t)) + sizeof(uint32_t);
  DynamicBuffer buf(serialized_len);

  HT_DEBUG_OUT <<"set_event_notification_handles txn="<< txn <<" event id="<< id
      << " num notification handles=" << handles.size() << HT_END;

  // Serialize handles vector
  Serialization::encode_i32(&buf.ptr, handles.size());
  for(uint32_t ii=0; ii< handles.size(); ++ii)
    Serialization::encode_i64(&buf.ptr, handles[ii]);

  Dbt data((void *)buf.base, serialized_len);

  try {
    HT_ASSERT(event_exists(txn, id));

    // Write event notification handles
    key_str = get_event_key(id, EVENT_NOTIFICATION_HANDLES);
    keym.set_str(key_str);
    // Write only if this key doesnt exist in the db
    ret = txn.handle_state_db->put(txn.db_txn, &keym, &data, DB_NOOVERWRITE);
    HT_ASSERT(ret == 0);

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}

/*
 *
 */
void
BerkeleyDbFilesystem::delete_event(BDbTxn &txn, uint64_t id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"delete_event txn="<< txn <<" event id="<< id << HT_END;
  try {
    if (!event_exists(txn, id))
      return;

    // Delete id under "/EVENTS/"
    String events_dir = EVENTS_STR;
    keym.set_str(events_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    cursorp->get(&keym, &datam, DB_GET_BOTH);
    cursorp->del(0);

    // Delete event type
    key_str = get_event_key(id, EVENT_TYPE);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete event mask
    key_str = get_event_key(id, EVENT_MASK);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete event name
    key_str = get_event_key(id, EVENT_NAME);
    keym.set_str(key_str);

    if ((ret = txn.handle_state_db->del(txn.db_txn, &keym, 0)) == DB_NOTFOUND)
      HT_DEBUG_OUT <<"txn="<< txn <<" event key " << keym.get_str()
                   << "not found in DB"  << HT_END;

    // Delete event mode
    key_str = get_event_key(id, EVENT_MODE);
    keym.set_str(key_str);

    if ((ret = txn.handle_state_db->del(txn.db_txn, &keym, 0)) == DB_NOTFOUND)
      HT_DEBUG_OUT <<"txn="<< txn <<" event key " << keym.get_str()
                   << " not found in DB"  << HT_END;

    // Delete event generation
    key_str = get_event_key(id, EVENT_GENERATION);
    keym.set_str(key_str);

    if ((ret = txn.handle_state_db->del(txn.db_txn, &keym, 0)) == DB_NOTFOUND)
      HT_DEBUG_OUT <<"txn="<< txn <<" event key " << keym.get_str()
                   << " not found in DB"  << HT_END;

    // Delete event notification handles
    key_str = get_event_key(id, EVENT_NOTIFICATION_HANDLES);
    keym.set_str(key_str);

    if ((ret = txn.handle_state_db->del(txn.db_txn, &keym, 0)) == DB_NOTFOUND)
      HT_DEBUG_OUT <<"txn="<< txn <<" event key " << keym.get_str()
                   << " not found in DB"  << HT_END;
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
}

/*
 *
 */
bool
BerkeleyDbFilesystem::event_exists(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool exists = true;
  char numbuf[16];

  HT_DEBUG_OUT <<"event_exists txn="<< txn << "event id=" << id << HT_END;

  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Check for id under "/EVENTS/"
    String events_dir = EVENTS_STR;
    keym.set_str(events_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    if(cursorp->get(&keym, &datam, DB_GET_BOTH) == DB_NOTFOUND) {
      exists = false;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }

  if(exists)
    HT_DEBUG_OUT <<"event id '"<< id <<"' exists."<< HT_END;

  return exists;
}

/*
 *
 */
void
BerkeleyDbFilesystem::create_session(BDbTxn &txn, uint64_t id, const String& addr)

{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  String expbuf;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"create_session txn="<< txn <<" create session addr='"<< addr
              << " id="<< id << HT_END;
  try {

    HT_ASSERT(!session_exists(txn, id));
    // Store id under "/SESSIONS/"
    String sessions_dir = SESSIONS_STR;
    keym.set_str(sessions_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);

    // Store session addr
    key_str = get_session_key(id, SESSION_ADDR);
    keym.set_str(key_str);
    datam.set_str(addr);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store session expired status
    key_str = get_session_key(id, SESSION_EXPIRED);
    keym.set_str(key_str);
    datam.set_str((String)"0");

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "exitting create_session txn="<< txn
              <<" create session addr='"<< addr << " id="<< id << HT_END;

}

/*
 *
 */
void
BerkeleyDbFilesystem::delete_session(BDbTxn &txn, uint64_t id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"delete_session txn="<< txn <<" session id="<< id << HT_END;
  try {

    // Delete session expired status
    key_str = get_session_key(id, SESSION_EXPIRED);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret==0);

    // Delete session handles
    String session_handles_dir = get_session_key(id, SESSION_HANDLES);
    keym.set_str(session_handles_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    ret = cursorp->get(&keym, &datam, DB_SET);
    while(ret != DB_NOTFOUND) {
      HT_ASSERT(ret==0);
      cursorp->del(0);
      ret = cursorp->get(&keym, &datam, DB_NEXT_DUP);
    }

    // Delete session name is exists
    key_str = get_session_key(id, SESSION_NAME);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    if (ret != DB_NOTFOUND) {
      ret = cursorp->del(0);
    }

    // Delete session addr
    key_str = get_session_key(id, SESSION_ADDR);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret==0);

    // Delete id under "/SESSIONS/"
    String sessions_dir = SESSIONS_STR;
    keym.set_str(sessions_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    cursorp->get(&keym, &datam, DB_GET_BOTH);
    ret = cursorp->del(0);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT << "exitting delete_session txn=" << txn << " session id=" << id << HT_END;
}

/*
 *
 */
void
BerkeleyDbFilesystem::expire_session(BDbTxn &txn, uint64_t id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"expire_session txn="<< txn <<" session id="<< id << HT_END;

  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    // Set session expired status
    key_str = get_session_key(id, SESSION_EXPIRED);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);
    datam.set_str((String)"1");

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "exitting expire_session txn="<< txn <<" session id=" << id << HT_END;

}

/*
 *
 */
void
BerkeleyDbFilesystem::add_session_handle(BDbTxn &txn, uint64_t id, uint64_t handle_id)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  char numbuf[17];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT << "add_session_handle txn="<< txn <<" session id="<< id
               << " handle id=" << handle_id << HT_END;
  try {
    HT_ASSERT(session_exists(txn, id));

    String session_handles_dir = get_session_key(id, SESSION_HANDLES);
    keym.set_str(session_handles_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"exitting add_session_handle txn="<< txn <<" session id="<< id
              << " handle id=" << handle_id << HT_END;
}

/*
 *
 */
void
BerkeleyDbFilesystem::get_session_handles(BDbTxn &txn, uint64_t id, std::vector<uint64_t> &handles)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"get_session_handles txn="<< txn <<" session id="<< id << HT_END;

  try {
    HT_ASSERT(session_exists(txn, id));

    String session_handles_dir = get_session_key(id, SESSION_HANDLES);
    keym.set_str(session_handles_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    ret = cursorp->get(&keym, &datam, DB_SET);
    while(ret != DB_NOTFOUND) {
      handles.push_back(strtoul((const char *)datam.get_data(), 0, 0));
      ret = cursorp->get(&keym, &datam, DB_NEXT_DUP);
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_session_handles txn="<< txn <<" session id="<< id << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::delete_session_handle(BDbTxn &txn, uint64_t id, uint64_t handle_id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[17];
  bool deleted = false;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"delete_session_handle txn="<< txn <<" session id="<< id
               << " handle_id=" << handle_id << HT_END;
  try {
    HT_ASSERT(session_exists(txn, id));

    // Delete session handle
    String session_handles_dir = get_session_key(id, SESSION_HANDLES);
    keym.set_str(session_handles_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_GET_BOTH);

    HT_EXPECT(ret == 0 || ret == DB_NOTFOUND, HYPERSPACE_STATEDB_ERROR);

    if (ret != DB_NOTFOUND) {
      ret = cursorp->del(0);
      HT_ASSERT(ret == 0);
      deleted = true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting delete_session_handle txn="<< txn <<" session id="<< id
               << " handle_id=" << handle_id << " deleted=" << deleted << HT_END;
  return deleted;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::session_exists(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool exists = true;
  char numbuf[16];

  HT_DEBUG_OUT <<"session_exists txn="<< txn << " session id=" << id << HT_END;

  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Check for id under "/SESSIONS/"
    String sessions_dir = SESSIONS_STR;
    keym.set_str(sessions_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    if(cursorp->get(&keym, &datam, DB_GET_BOTH) == DB_NOTFOUND) {
      exists = false;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }

  if(exists)
    HT_DEBUG_OUT <<"session id '"<< id <<"' exists."<< HT_END;

  HT_DEBUG_OUT <<"exitting session_exists txn="<< txn << " session id=" << id << HT_END;
  return exists;
}

/*
 *
 */
void
BerkeleyDbFilesystem::set_session_name(BDbTxn &txn, uint64_t id, const String &name)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"set_session_name txn="<< txn <<" name='"<< name << "' id="<< id << HT_END;
  try {

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    // Store session name/replace if exists
    key_str = get_session_key(id, SESSION_NAME);
    keym.set_str(key_str);
    ret = cursorp->get(&keym, &datam, DB_SET);
    datam.set_str(name);
    if (ret == DB_NOTFOUND)
      ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    else
      ret = cursorp->put(&keym, &datam, DB_CURRENT);

    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_session_name txn="<< txn <<" name='"<< name << "'" << HT_END;
}

/*
 *
 */
String
BerkeleyDbFilesystem::get_session_name(BDbTxn &txn, uint64_t id)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str, name;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"get_session_name txn="<< txn <<" session id="<< id << HT_END;

  try {
    // Throw exception if session does not exist
    HT_EXPECT(session_exists(txn, id), HYPERSPACE_STATEDB_SESSION_NOT_EXISTS);

    keym.set_str(get_session_key(id, SESSION_NAME));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    ret = cursorp->get(&keym, &datam, DB_SET);

    HT_ASSERT(ret == 0 || ret == DB_NOTFOUND);

    if (ret == DB_NOTFOUND)
      name = "";
    else
      name = datam.get_str();
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_session_name txn="<< txn
               <<" session id=" << id << " name="<< name << HT_END;
  return name;
}

/*
 *
 */
void
BerkeleyDbFilesystem::create_handle(BDbTxn &txn, uint64_t id, String node_name,
    uint32_t open_flags, uint32_t event_mask, uint64_t session_id,
    bool locked, uint32_t del_state)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[17];
  String buf;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"create_handle txn="<< txn <<" id="<< id << " node='"<< node_name
               << "' open_flags=" << open_flags << " event_mask="<< event_mask
               << " session_id=" << session_id << " locked=" << locked
               << " del_state=" << del_state << HT_END;
  try {

    HT_ASSERT(!handle_exists(txn, id));
    // Store id under "/HANDLES/"
    String handles_dir = HANDLES_STR;
    keym.set_str(handles_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);

    // Store handle node name
    key_str = get_handle_key(id, HANDLE_NODE_NAME);
    keym.set_str(key_str);
    datam.set_str(node_name);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store handle open_flags
    key_str = get_handle_key(id, HANDLE_OPEN_FLAGS);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)open_flags);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store handle deletion state
    key_str = get_handle_key(id, HANDLE_DEL_STATE);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)del_state);
    datam.set_str(numbuf);

    ret =  txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store handle notification event_mask
    key_str = get_handle_key(id, HANDLE_EVENT_MASK);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)event_mask);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store handle session id
    key_str = get_handle_key(id, HANDLE_SESSION_ID);
    keym.set_str(key_str);
    sprintf(numbuf, "%llu", (Llu)session_id);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store handle locked bool
    key_str = get_handle_key(id, HANDLE_LOCKED);
    keym.set_str(key_str);
    if(locked)
      buf = "1";
    else
      buf = "0";
    datam.set_str(buf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting create_handle txn="<< txn <<" id="<< id << " node='"<< node_name
               << "' open_flags=" << open_flags << " event_mask="<< event_mask
               << " session_id=" << session_id << " locked=" << locked
               << " del_state=" << del_state << HT_END;
}

/*
 *
 */
void
BerkeleyDbFilesystem::delete_handle(BDbTxn &txn, uint64_t id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"delete_handle txn="<< txn <<" handle id="<< id << HT_END;
  try {
    if (!handle_exists(txn, id))
      return;
    // Delete handle locked bool
    key_str = get_handle_key(id, HANDLE_LOCKED);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete handle session id
    key_str = get_handle_key(id, HANDLE_SESSION_ID);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete handle notification event_mask
    key_str = get_handle_key(id, HANDLE_EVENT_MASK);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete handle open_flags
    key_str = get_handle_key(id, HANDLE_OPEN_FLAGS);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete handle deletion state
    key_str = get_handle_key(id, HANDLE_DEL_STATE);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete handle node name
    key_str = get_handle_key(id, HANDLE_NODE_NAME);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete id under "/HANDLES/"
    String handles_dir = HANDLES_STR;
    keym.set_str(handles_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_GET_BOTH);
    HT_ASSERT(ret==0);
    ret = cursorp->del(0);
    HT_ASSERT(ret==0);

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT << "exitting delete_handle txn="<< txn <<" handle id="
      << id << HT_END;

}

/*
 *
 */
void
BerkeleyDbFilesystem::set_handle_del_state(BDbTxn &txn, uint64_t id, uint32_t del_state)
{
  DbtManaged keym, datam;
  char numbuf[17];
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_handle_del_state txn="<< txn <<" handle id="
               << id << " del_state=" << del_state << HT_END;
  try {
    HT_ASSERT(handle_exists(txn, id));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing open flag
    key_str = get_handle_key(id, HANDLE_DEL_STATE);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    sprintf(numbuf, "%lu", (Lu)del_state);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    HT_ERRORF("Berkeley DB error: %s", e.what());
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else
      HT_THROW(HYPERSPACE_STATEDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_handle_del_state txn="<< txn <<" handle id="
               << id << " del_state=" << del_state << HT_END;

}

/*
 *
 */
void
BerkeleyDbFilesystem::set_handle_open_flags(BDbTxn &txn, uint64_t id, uint32_t open_flags)
{
  DbtManaged keym, datam;
  char numbuf[17];
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_handle_open_flags txn="<< txn <<" handle id="
               << id << " open_flags=" << open_flags << HT_END;
  try {
    HT_EXPECT(handle_exists(txn, id), HYPERSPACE_STATEDB_HANDLE_NOT_EXISTS);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing open flag
    key_str = get_handle_key(id, HANDLE_OPEN_FLAGS);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    sprintf(numbuf, "%lu", (Lu)open_flags);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_handle_open_flags txn="<< txn <<" handle id="
               << id << " open_flags=" << open_flags << HT_END;

}

/*
 *
 */
void
BerkeleyDbFilesystem::set_handle_event_mask(BDbTxn &txn, uint64_t id, uint32_t event_mask)
{
  DbtManaged keym, datam;
  char numbuf[17];
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_handle_event_mask txn="<< txn <<" handle id="
               << id << " event_mask=" << event_mask << HT_END;
  try {
    HT_EXPECT(handle_exists(txn, id), HYPERSPACE_STATEDB_HANDLE_NOT_EXISTS);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing event_mask
    key_str = get_handle_key(id, HANDLE_EVENT_MASK);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    sprintf(numbuf, "%lu", (Lu)event_mask);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_handle_event_mask txn="<< txn <<" handle id="
               << id << " event_mask=" << event_mask << HT_END;

}

/*
 *
 */
uint32_t
BerkeleyDbFilesystem::get_handle_event_mask(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;
  uint32_t event_mask;

  HT_DEBUG_OUT <<"get_handle_event_mask txn="<< txn <<" handle id=" << id << HT_END;

  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing event_mask
    key_str = get_handle_key(id, HANDLE_EVENT_MASK);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);
    event_mask = (uint32_t)strtoull(datam.get_str(), 0, 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_handle_event_mask txn="<< txn <<" handle id=" << id
               << " event_mask=" << event_mask << HT_END;

  return event_mask;
}

/*
 *
 */
void
BerkeleyDbFilesystem::set_handle_locked(BDbTxn &txn, uint64_t id, bool locked)
{
  DbtManaged keym, datam;
  int ret;
  String buf;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_handle_locked txn="<< txn <<" handle id=" << id
               << " locked=" << locked << HT_END;
  try {
    HT_ASSERT(handle_exists(txn, id));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing lockedness
    key_str = get_handle_key(id, HANDLE_LOCKED);
    keym.set_str(key_str);
    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);
    if(locked)
      buf = "1";
    else
      buf = "0";
    datam.set_str(buf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
 }
 HT_DEBUG_OUT <<"exitting set_handle_locked txn="<< txn <<" handle id=" << id
              << " locked=" << locked << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::handle_exists(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool exists = true;
  char numbuf[17];

  HT_DEBUG_OUT <<"handle_exists txn="<< txn << " handle id=" << id << HT_END;

  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Check for id under "/HANDLES/"
    String handles_dir = HANDLES_STR;
    keym.set_str(handles_dir);
    sprintf(numbuf, "%llu", (Llu)id);
    datam.set_str(numbuf);

    if(cursorp->get(&keym, &datam, DB_GET_BOTH) == DB_NOTFOUND) {
      exists = false;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
   }

  HT_DEBUG_OUT << "exitting handle_exists txn="<< txn << " handle id=" << id
               << " exists=" << exists << HT_END;
  return exists;
}

/*
 *
 */

bool
BerkeleyDbFilesystem::handle_is_locked(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  int ret;
  String buf;
  bool locked;
  String key_str;

  HT_DEBUG_OUT << "handle_is_locked txn=" << txn << " handle id=" << id << HT_END;

  try {
    HT_ASSERT(handle_exists(txn, id));

    // Get locked-ness
    key_str = get_handle_key(id, HANDLE_LOCKED);
    keym.set_str(key_str);
    ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    buf = datam.get_str();

    if (buf == "1")
      locked = true;
    else if (buf == "0")
      locked = false;
    else {
      HT_ASSERT(false);
    }

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_handle_locked txn="<< txn <<" handle id=" << id
               << " locked=" << locked << HT_END;
  return locked;
}

/*
 *
 */
void
BerkeleyDbFilesystem::get_handle_node(BDbTxn &txn, uint64_t id, String &node_name)
{
  DbtManaged keym, datam;
  int ret;
  String key_str;

  HT_DEBUG_OUT <<"get_handle_node_name txn="<< txn <<" handle id=" << id << HT_END;

  try {
    HT_ASSERT(handle_exists(txn, id));

    // Get existing node_name
    key_str = get_handle_key(id, HANDLE_NODE_NAME);
    keym.set_str(key_str);

    ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
    node_name = datam.get_str();

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_handle_node_name txn="<< txn <<" handle id=" << id
               <<" node_name=" << node_name << HT_END;
}

/*
 *
 */
uint32_t
BerkeleyDbFilesystem::get_handle_del_state(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  int ret;
  String key_str;
  int del_state;

  HT_DEBUG_OUT <<"get_handle_del_state txn="<< txn <<" handle id=" << id << HT_END;

  try {
    HT_ASSERT(handle_exists(txn, id));

    // Get existing deletion state
    key_str = get_handle_key(id, HANDLE_DEL_STATE);
    keym.set_str(key_str);

    ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    del_state = (uint32_t)strtoul(datam.get_str(), 0, 0);
  }
  catch (DbException &e) {
    HT_ERRORF("Berkeley DB error: %s", e.what());
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else
      HT_THROW(HYPERSPACE_STATEDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_handle_del_state txn="<< txn <<" handle id="
               << id << " del_state=" << del_state << HT_END;
  return del_state;
}

/*
 *
 */
uint32_t
BerkeleyDbFilesystem::get_handle_open_flags(BDbTxn &txn, uint64_t id)
{
  DbtManaged keym, datam;
  int ret;
  String key_str;
  uint32_t open_flags;

  HT_DEBUG_OUT <<"get_handle_open_flags txn="<< txn <<" handle id=" << id << HT_END;

  try {
    HT_ASSERT(handle_exists(txn, id));

    // Get existing open flag
    key_str = get_handle_key(id, HANDLE_OPEN_FLAGS);
    keym.set_str(key_str);

    ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    open_flags = (uint32_t)strtoul(datam.get_str(), 0, 0);

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_handle_open_flags txn="<< txn <<" handle id="
               << id << " open_flags=" << open_flags << HT_END;
  return open_flags;
}

/*
 *
 */
uint64_t
BerkeleyDbFilesystem::get_handle_session(BDbTxn &txn, uint64_t id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  uint64_t session_id;

  HT_DEBUG_OUT <<"get_handle_session txn="<< txn <<" id="<< id << HT_END;

  try {

    HT_ASSERT(handle_exists(txn, id));

    // Get handle session id
    key_str = get_handle_key(id, HANDLE_SESSION_ID);
    keym.set_str(key_str);

    ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    session_id = (uint64_t)strtoull(datam.get_str(), 0, 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"get_handle_session txn="<< txn <<" id="<< id << " session_id="
               << session_id << HT_END;

  return session_id;
}


/*
 *
 */
void
BerkeleyDbFilesystem::create_node(BDbTxn &txn, const String &name,
    bool ephemeral, uint64_t lock_generation,  uint32_t cur_lock_mode,
    uint64_t exclusive_handle)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String buf;
  char numbuf[17];

  HT_DEBUG_OUT <<"create_node txn="<< txn <<" create node ='"<< name
               << " ephemeral=" << ephemeral << " lock_mode=" << cur_lock_mode
               << " lock_generation=" << lock_generation << " exclusive_handle="
               << " exclusive_handle" << HT_END;

  try {
    HT_ASSERT(!node_exists(txn, name));
    // Store id under "/NODES/"
    String nodes_dir = NODES_STR;
    keym.set_str(nodes_dir);
    datam.set_str(name);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);

    // Store node ephemeral
    key_str = get_node_key(name, NODE_EPHEMERAL);
    keym.set_str(key_str);
    if(ephemeral)
      buf = "1";
    else
      buf = "0";
    datam.set_str(buf);
    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);


    // Store node cur lock mode
    key_str = get_node_key(name, NODE_LOCK_MODE);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)cur_lock_mode);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store node lock generation
    key_str = get_node_key(name, NODE_LOCK_GENERATION);
    keym.set_str(key_str);
    sprintf(numbuf, "%llu", (Llu)lock_generation);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

    // Store node exclusive_handle
    key_str = get_node_key(name, NODE_EXCLUSIVE_LOCK_HANDLE);
    keym.set_str(key_str);
    sprintf(numbuf, "%llu", (Llu)exclusive_handle);
    datam.set_str(numbuf);

    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);

  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT << "exitting create_node txn="<< txn <<" create node ='"
               << name << "' ephemeral=" << ephemeral << " lock_mode="
               << cur_lock_mode
               << " lock_generation=" << lock_generation << " exclusive_handle="
               << " exclusive_handle" << HT_END;

}

/*
 *
 */
void
BerkeleyDbFilesystem::set_node_lock_generation(BDbTxn &txn, const String &name,
                                               uint64_t lock_generation)
{
  DbtManaged keym, datam;
  char numbuf[17];
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  String key_str;

  HT_DEBUG_OUT <<"set_node_lock_generation txn="<< txn <<" node=" << name
               <<" lock_generation=" << lock_generation << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing lock_generation
    key_str = get_node_key(name, NODE_LOCK_GENERATION);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    sprintf(numbuf, "%llu", (Llu)lock_generation);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_node_lock_generation txn="<< txn <<" node=" << name
               <<" lock_generation=" << lock_generation << HT_END;
}

/*
 *
 */
uint64_t
BerkeleyDbFilesystem::incr_node_lock_generation(BDbTxn &txn, const String &name)
{
  DbtManaged keym, datam;
  char numbuf[17];
  int ret;
  uint64_t lock_generation=0;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  String key_str;

  HT_DEBUG_OUT <<"incr_node_lock_generation txn="<< txn <<" node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing lock_generation
    key_str = get_node_key(name, NODE_LOCK_GENERATION);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    lock_generation = (uint64_t)strtoull(datam.get_str(), 0, 0);
    ++lock_generation;
    sprintf(numbuf, "%llu", (Llu)lock_generation);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting incr_node_lock_generation txn="<< txn <<" node="<< name
               <<" lock_generation=" << lock_generation << HT_END;

  return lock_generation;
}

/*
 *
 */
void
BerkeleyDbFilesystem::set_node_ephemeral(BDbTxn &txn, const String &name,
                                         bool ephemeral)
{
  DbtManaged keym, datam;
  int ret;
  String buf;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_node_ephemeral txn="<< txn <<" node_name=" << name
               <<" ephemeral=" << ephemeral << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing node_name
    key_str = get_node_key(name, NODE_EPHEMERAL);
    keym.set_str(key_str);
    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);
    if(ephemeral)
      buf = "1";
    else
      buf = "0";
    datam.set_str(buf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting set_node_ephemeral txn="<< txn <<" node_name=" << name
               <<" ephemeral=" << ephemeral << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::node_is_ephemeral(BDbTxn &txn, const String &name)
{
  DbtManaged keym, datam;
  int ret;
  String buf;
  String key_str;
  bool ephemeral;

  HT_DEBUG_OUT << "node_is_ephemeral txn=" << txn << " node_name=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    // Replace existing node_name
    key_str = get_node_key(name, NODE_EPHEMERAL);
    keym.set_str(key_str);
    ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);

    HT_ASSERT(ret == 0);

    if (!strcmp(datam.get_str(), "0"))
      ephemeral = false;
    else if (!strcmp(datam.get_str(), "1"))
      ephemeral = true;
    else
      HT_ASSERT(false);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "exitting node_is_ephemeral txn=" << txn << " node_name= " << name
               << " ephemeral=" << ephemeral << HT_END;
  return ephemeral;
}

/*
 *
 */
void
BerkeleyDbFilesystem::set_node_cur_lock_mode(BDbTxn &txn, const String &name,
    uint32_t lock_mode)
{
  DbtManaged keym, datam;
  char numbuf[16];
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_node_cur_lock_mode txn="<< txn <<" node=" << name
               <<" lock_mode=" << lock_mode << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing lock_mode
    key_str = get_node_key(name, NODE_LOCK_MODE);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    sprintf(numbuf, "%lu", (Lu)lock_mode);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "exitting set_node_cur_lock_mode txn="<< txn <<" node=" << name
               << " lock_mode=" << lock_mode << HT_END;
}

/*
 *
 */
uint32_t
BerkeleyDbFilesystem::get_node_cur_lock_mode(BDbTxn &txn, const String &name)
{
  DbtManaged keym, datam;
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;
  uint32_t lock_mode;

  HT_DEBUG_OUT <<"get_node_cur_lock_mode txn="<< txn <<" node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Get existing lock_mode
    key_str = get_node_key(name, NODE_LOCK_MODE);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);
    lock_mode = (uint32_t) strtoull(datam.get_str(), 0, 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "exitting get_node_cur_lock_mode txn="<< txn <<" node=" << name
               << " lock_mode=" << lock_mode << HT_END;
  return lock_mode;
}

/*
 *
 */
void
BerkeleyDbFilesystem::set_node_exclusive_lock_handle(BDbTxn &txn,
    const String &name, uint64_t exclusive_lock_handle)
{
  DbtManaged keym, datam;
  char numbuf[17];
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;

  HT_DEBUG_OUT <<"set_node_exclusive_lock_handle  txn="<< txn <<" node=" << name
               <<" exclusive_lock_handle=" << exclusive_lock_handle << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing exclusive lock handle
    key_str = get_node_key(name, NODE_EXCLUSIVE_LOCK_HANDLE);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);

    sprintf(numbuf, "%llu", (Llu)exclusive_lock_handle);
    datam.set_str(numbuf);

    ret = cursorp->put(&keym, &datam, DB_CURRENT);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"exitting set_node_exclusive_lock_handle  txn="<< txn <<" node="<< name
               <<" exclusive_lock_handle=" << exclusive_lock_handle << HT_END;
}

/*
 *
 */
uint64_t
BerkeleyDbFilesystem::get_node_exclusive_lock_handle(BDbTxn &txn, const String &name)
{
  DbtManaged keym, datam;
  int ret;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  String key_str;
  uint64_t exclusive_lock_handle=0;

  HT_DEBUG_OUT <<"get_node_exclusive_lock_handle  txn="<< txn <<" node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Replace existing exclusive lock handle
    key_str = get_node_key(name, NODE_EXCLUSIVE_LOCK_HANDLE);
    keym.set_str(key_str);

    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0);
    exclusive_lock_handle = (uint64_t)strtoull(datam.get_str(), 0, 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting get_node_exclusive_lock_handle  txn=" << txn <<" node=" << name
               <<" exclusive_lock_handle=" << exclusive_lock_handle << HT_END;

  return exclusive_lock_handle;
}

/*
 *
 */
void
BerkeleyDbFilesystem::add_node_handle(BDbTxn &txn, const String &name,
    uint64_t handle_id)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"add_node_handle txn="<< txn <<" node="<< name << " handle id=" << handle_id
               << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));

    String node_handles_dir = get_node_key(name,
        NODE_HANDLE_MAP);
    keym.set_str(node_handles_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"exitting add_node_handle txn="<< txn <<" node="<< name
               << " handle id=" << handle_id << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::get_node_event_notification_map(BDbTxn &txn, const String &name,
    uint32_t event_mask, NotificationMap &handles_to_sessions)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  uint64_t handle, session;
  uint32_t mask;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool has_notifications = false;

  HT_DEBUG_OUT <<"get_node_event_notification_map txn="<< txn <<" node="<< name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    String node_handles_dir = get_node_key(name, NODE_HANDLE_MAP);
    keym.set_str(node_handles_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_SET);

    // iterate through all handles
    while (ret != DB_NOTFOUND) {
      HT_ASSERT(ret==0);
      handle  = (uint64_t)strtoull(datam.get_str(), 0, 0);
      mask = get_handle_event_mask(txn, handle);
      // if this handle was registered for this event then store it in the notification map
      if ((mask&event_mask) != 0) {
        session = get_handle_session(txn, handle);
        handles_to_sessions[handle] = session;
        has_notifications = true;
      }
      ret = cursorp->get(&keym, &datam, DB_NEXT_DUP);
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "exitting get_node_event_notification_map txn=" << txn << " node="<< name
               << " has_notifications=" << has_notifications << HT_END;
  return has_notifications;
}

/*
 *
 */
void
BerkeleyDbFilesystem::delete_node_handle(BDbTxn &txn, const String &name,
    uint64_t handle_id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp=0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  char numbuf[17];

  HT_DEBUG_OUT <<"delete_node_handle txn="<< txn <<" node="<< name
               <<" handle_id=" << handle_id << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));

    // Delete handle
    String node_handles_dir = get_node_key(name, NODE_HANDLE_MAP);
    keym.set_str(node_handles_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_GET_BOTH);
    HT_ASSERT(ret ==0 );

    ret = cursorp->del(0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"exitting delete_node_handle txn="<< txn <<" node="<< name
               <<" handle_id=" << handle_id << HT_END;
}



/*
 *
 */
void
BerkeleyDbFilesystem::add_node_pending_lock_request(BDbTxn &txn,
                                                    const String &name,
                                                    LockRequest &request) {
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[17];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"add_node_pending_lock_request txn="<< txn <<" node=" << name
               << " handle id=" << request.handle << " mode=" << request.mode << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));

    String node_pending_locks_dir = get_node_key(name, NODE_PENDING_LOCK_REQUESTS);
    keym.set_str(node_pending_locks_dir);
    sprintf(numbuf, "%llu", (Llu)request.handle);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);

    key_str = get_node_pending_lock_request_key(name, request.handle);
    keym.set_str(key_str);
    sprintf(numbuf, "%lu", (Lu)request.mode);
    datam.set_str(numbuf);
    ret = txn.handle_state_db->put(txn.db_txn, &keym, &datam, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<" exitting add_node_pending_lock_request txn="<< txn << " node=" << name
               <<" handle id=" << request.handle << " mode=" << request.mode << HT_END;
}

/*
 *
 */

bool
BerkeleyDbFilesystem::node_has_pending_lock_request(BDbTxn &txn, const String &name)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  bool has_pending_lock_request = false;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  uint64_t handle_id;

  HT_DEBUG_OUT << "node_has_pending_lock_request txn=" << txn << " node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    // get top pending handle id of lock request (if any)
    String node_pending_locks_dir = get_node_key(name, NODE_PENDING_LOCK_REQUESTS);
    keym.set_str(node_pending_locks_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_SET);

    HT_ASSERT(ret == 0 || ret == DB_NOTFOUND);

    if (ret == 0) {
      // get lock request mode
      handle_id = (uint64_t) strtoull(datam.get_str(), 0, 0);
      key_str = get_node_pending_lock_request_key(name, handle_id);
      keym.set_str(key_str);
      ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
      HT_ASSERT(ret == 0);
      has_pending_lock_request = true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<" exitting node_has_pending_lock_request txn="<< txn << " node=" << name
               <<" node_has_pending_lock_request=" << has_pending_lock_request << HT_END;

  return has_pending_lock_request;
}

/*
 *
 */

bool
BerkeleyDbFilesystem::get_node_pending_lock_request(BDbTxn &txn, const String &name,
    LockRequest &front_req)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  bool has_pending_lock_request = false;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  uint64_t handle_id;

  HT_DEBUG_OUT << "get_node_pending_lock_request txn=" << txn << " node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    // get top pending handle id of lock request (if any)
    String node_pending_locks_dir = get_node_key(name, NODE_PENDING_LOCK_REQUESTS);
    keym.set_str(node_pending_locks_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0 || ret == DB_NOTFOUND);

    if (ret == 0) {
      // get lock request mode
      has_pending_lock_request = true;
      handle_id = (uint64_t) strtoull(datam.get_str(), 0, 0);
      key_str = get_node_pending_lock_request_key(name, handle_id);
      keym.set_str(key_str);
      ret = txn.handle_state_db->get(txn.db_txn, &keym, &datam, 0);
      HT_ASSERT(ret == 0);

      front_req.handle = handle_id;
      front_req.mode = (uint32_t) strtoull(datam.get_str(), 0, 0);
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<" exitting get_node_pending_lock_request txn="<< txn << " node=" << name
               <<" has_pending_lock_request=" << has_pending_lock_request << HT_END;

  return has_pending_lock_request;
}
/*
 *
 */
void
BerkeleyDbFilesystem::delete_node_pending_lock_request(BDbTxn &txn,
    const String &name, uint64_t handle_id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"remove_node_pending_lock_request txn="<< txn <<" node=" << name
               <<" handle id=" << handle_id << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));

    // Delete node pending lock req
    String node_pending_locks_dir = get_node_key(name,
        NODE_PENDING_LOCK_REQUESTS);
    keym.set_str(node_pending_locks_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_GET_BOTH);
    HT_ASSERT(ret ==0);

    ret = cursorp->del(0);
    HT_ASSERT(ret == 0);
    //Delete pending lock request
    key_str = get_node_pending_lock_request_key(name, handle_id);
    keym.set_str(key_str);
    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret==0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting remove_node_pending_lock_request txn="<< txn
               <<" node=" << name << " handle id=" << handle_id << HT_END;
}

/*
 *
 */
void
BerkeleyDbFilesystem::add_node_shared_lock_handle(BDbTxn &txn, const String &name,
                                                  uint64_t handle_id)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  char numbuf[16];
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"add_node_shared_lock_handle txn="<< txn <<" node="<< name
      << " handle id=" << handle_id << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));

    String node_shared_handles_dir = get_node_key(name,
        NODE_SHARED_LOCK_HANDLES);
    keym.set_str(node_shared_handles_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->put(&keym, &datam, DB_KEYLAST);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"exitting add_node_shared_lock_handle txn="<< txn <<" node="<< name
               << " handle id=" << handle_id << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::node_has_shared_lock_handles(BDbTxn &txn, const String &name)
{
  int ret;
  DbtManaged  keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool has_shared_lock_handles=false;

  HT_DEBUG_OUT <<"node_has_shared_lock_handles txn="<< txn <<" node="<< name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    String node_shared_handles_dir = get_node_key(name, NODE_SHARED_LOCK_HANDLES);
    keym.set_str(node_shared_handles_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_SET);
    HT_ASSERT(ret == 0 || ret == DB_NOTFOUND);

    if (ret != DB_NOTFOUND)
      has_shared_lock_handles = true;
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting node_has_shared_lock_handle txn="<< txn <<" node="<< name
               <<" has_shared_lock_handles=" << has_shared_lock_handles << HT_END;
  return has_shared_lock_handles;
}

/*
 *
 */
void
BerkeleyDbFilesystem::delete_node_shared_lock_handle(BDbTxn &txn, const String &name,
                                                     uint64_t handle_id)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp=0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  char numbuf[17];

  HT_DEBUG_OUT <<"remove_node_shared_lock_handle txn="<< txn <<" node="<< name
               <<" handle_id=" << handle_id << HT_END;
  try {
    HT_ASSERT(node_exists(txn, name));

    // Delete shared lock handle
    String node_shared_handles_dir = get_node_key(name, NODE_SHARED_LOCK_HANDLES );
    keym.set_str(node_shared_handles_dir);
    sprintf(numbuf, "%llu", (Llu)handle_id);
    datam.set_str(numbuf);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_GET_BOTH);
    HT_ASSERT(ret ==0);

    ret = cursorp->del(0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT <<"exitting remove_node_shared_lock_handle txn="<< txn <<" node="<< name
               <<" handle_id=" << handle_id << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::delete_node(BDbTxn &txn, const String &name)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT <<"delete_node txn="<< txn <<" node ="<< name << HT_END;
  try {
    if (!node_exists(txn, name))
      return false;
    // Delete name under "/NODES/"
    String nodes_dir = NODES_STR;
    keym.set_str(nodes_dir);
    datam.set_str(name);

    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_GET_BOTH);
    HT_ASSERT(ret==0);
    ret = cursorp->del(0);
    HT_ASSERT(ret==0);

    // Delete node ephemeral bool
    key_str = get_node_key(name, NODE_EPHEMERAL);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete node cur_lock_mode
    key_str = get_node_key(name, NODE_LOCK_MODE);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete node lock_generation
    key_str = get_node_key(name, NODE_LOCK_GENERATION);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);

    // Delete node exclusive_lock_handle
    key_str = get_node_key(name, NODE_EXCLUSIVE_LOCK_HANDLE);
    keym.set_str(key_str);

    ret = txn.handle_state_db->del(txn.db_txn, &keym, 0);
    HT_ASSERT(ret == 0);
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }
  HT_DEBUG_OUT << "exitting delete_node txn="<< txn <<" node="
      << name << HT_END;
  return true;
}


/*
 *
 */
bool
BerkeleyDbFilesystem::node_exists(BDbTxn &txn, const String &name)
{
  DbtManaged keym, datam;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool exists = true;

  HT_DEBUG_OUT <<"node_exists txn="<< txn << " node name="
      << name << HT_END;

  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Check for id under "/NODES/"
    String nodes_dir = NODES_STR;
    keym.set_str(nodes_dir);
    datam.set_str(name);

    if(cursorp->get(&keym, &datam, DB_GET_BOTH) == DB_NOTFOUND) {
      exists = false;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"exitting node_exists txn="<< txn << " node name =" << name
               <<" exists=" << exists << HT_END;
  return exists;
}

/*
 *
 */
void
BerkeleyDbFilesystem::get_node_handles(BDbTxn &txn, const String &name,
                                       std::vector<uint64_t> &handles)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);

  HT_DEBUG_OUT << "get_node_handles txn=" << txn << " node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    // Iterate through all handles
    String node_handles_dir = get_node_key(name, NODE_HANDLE_MAP);
    keym.set_str(node_handles_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_SET);

    while (ret != DB_NOTFOUND) {
      HT_ASSERT(ret==0);
      handles.push_back((uint64_t)strtoull(datam.get_str(), 0, 0));
      ret = cursorp->get(&keym, &datam, DB_NEXT_DUP);
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "get_node_handles txn=" << txn << " node=" << name << HT_END;
}

/*
 *
 */
bool
BerkeleyDbFilesystem::node_has_open_handles(BDbTxn &txn, const String &name)
{
  int ret;
  DbtManaged keym, datam;
  String key_str;
  Dbc *cursorp=0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  bool has_open_handles = false;
  uint64_t open_handle;

  HT_DEBUG_OUT << "node_has_open_handles txn=" << txn << " node=" << name << HT_END;

  try {
    HT_ASSERT(node_exists(txn, name));

    // Check to see if there is even one handle open to this node
    String node_handles_dir = get_node_key(name, NODE_HANDLE_MAP);
    keym.set_str(node_handles_dir);
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);
    ret = cursorp->get(&keym, &datam, DB_SET);

    HT_ASSERT(ret == 0 || ret == DB_NOTFOUND);
    if (ret == 0) {
      open_handle = (uint64_t) strtoull((const char *)datam.get_data(), 0, 0);
      HT_DEBUG_OUT << "node_has_open_handles txn=" << txn << " node=" << name
                   << " at least one open_handle=" << open_handle << HT_END;
      has_open_handles = true;
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT << "node_has_open_handles txn=" << txn << " node=" << name
               << " has_open_handles=" << has_open_handles << HT_END;
  return has_open_handles;
}

/*
 *
 */
uint64_t
BerkeleyDbFilesystem::get_next_id_i64(BDbTxn &txn, IdentifierType id_type, bool increment)
{
  DbtManaged keym, datam;
  int ret;
  uint64_t retval=0;
  Dbc *cursorp = 0;
  HT_ON_SCOPE_EXIT(&close_db_cursor, &cursorp);
  char numbuf[17];

  HT_DEBUG_OUT <<"get_next_id_i64 txn="<< txn << " id_type=" << id_type << " increment="
               << increment << HT_END;
  try {
    txn.handle_state_db->cursor(txn.db_txn, &cursorp, 0);

    // Get next id
    switch (id_type) {
      case SESSION:
        keym.set_str(NEXT_SESSION_ID);
        break;
      case HANDLE:
        keym.set_str(NEXT_HANDLE_ID);
        break;
      case EVENT:
        keym.set_str(NEXT_EVENT_ID);
        break;
      default:
        HT_THROWF(HYPERSPACE_STATEDB_BAD_KEY, "Unknown i64 id type:%d",id_type);
    }

    ret = cursorp->get(&keym, &datam, DB_SET);

    HT_ASSERT(ret == 0);

    retval = strtoull((const char *)datam.get_data(), 0, 0);
    if (increment) {
      sprintf(numbuf, "%llu", (Llu)retval+1);
      datam.set_str(numbuf);
      ret = cursorp->put(&keym, &datam, DB_CURRENT);
      HT_ASSERT(ret==0);
    }
  }
  catch (DbException &e) {
    if (e.get_errno() == DB_LOCK_DEADLOCK)
      HT_THROW(HYPERSPACE_BERKELEYDB_DEADLOCK, e.what());
    else if (e.get_errno() == DB_REP_HANDLE_DEAD)
      HT_THROW(HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD, e.what());
    HT_ERRORF("Berkeley DB error: %s", e.what());
    HT_THROW(HYPERSPACE_BERKELEYDB_ERROR, e.what());
  }

  HT_DEBUG_OUT <<"get_next_id_i64 txn="<< txn << " id_type=" << id_type << " increment="
               << increment << " retval=" << retval << HT_END;
  return retval;
}

std::ostream& Hyperspace::operator<<(std::ostream &out, const BDbTxn &txn) {
  out << "{BDbTxn m_handle_namespace_db=" << txn.handle_namespace_db
      << ", m_handle_state_db=" << txn.handle_state_db
      << ", m_db_txn=" << txn.db_txn << "}";
  return out;
}
