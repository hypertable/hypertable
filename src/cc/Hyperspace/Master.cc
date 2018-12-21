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

#include "Config.h"
#include "Event.h"
#include "Notification.h"
#include "Master.h"
#include "Session.h"
#include "SessionData.h"

#include <Common/Thread.h>
#include <Common/Error.h>
#include <Common/Path.h>
#include <Common/FileUtils.h>
#include <Common/StringExt.h>
#include <Common/Random.h>
#include <Common/SystemInfo.h>

#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>

extern "C" {
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__FreeBSD__)
#include <sys/extattr.h>
#else
#include <sys/xattr.h>
#endif
#include <unistd.h>
}

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace Hyperspace;
using namespace std;

#define HT_BDBTXN_BEGIN(parent_txn) \
  do { \
    BDbTxn txn;\
    std::stringstream txn_str;\
    HT_ASSERT(is_master());\
    m_bdb_fs->start_transaction(txn); \
    try

#define HT_BDBTXN_END_CB(_cb_) \
    catch (Exception &e) { \
      if (e.code() == Error::HYPERSPACE_BERKELEYDB_DEADLOCK) { \
        txn_str << txn; \
        HT_INFOF("Berkeley DB deadlock encountered in txn %s", txn_str.str().c_str()); \
        txn.abort(); \
        this_thread::sleep_for(Random::duration_millis(3000)); \
        continue; \
      }\
      else if (e.code() == Error::HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD) { \
        txn_str << txn; \
        HT_INFOF("Berkeley DB rep handle dead deadlock encountered in txn %s", txn_str.str().c_str()); \
        txn.abort(); \
        continue; \
      }\
      else if (e.code() == Error::HYPERSPACE_BERKELEYDB_ERROR) \
        HT_ERROR_OUT << e << HT_END; \
      else \
        HT_ERRORF("%s - %s", Error::get_text(e.code()), e.what()); \
      txn.abort(); \
      _cb_->error(e.code(), e.what()); \
      return; \
    } \
    HT_DEBUG_OUT << "end txn " << txn << HT_END; \
    break; \
  } while (true)

#define HT_BDBTXN_END(...) \
    catch (Exception &e) { \
      if (e.code() == Error::HYPERSPACE_BERKELEYDB_DEADLOCK) {\
        txn_str << txn; \
        HT_INFOF("Berkeley DB deadlock encountered in txn %s", txn_str.str().c_str()); \
        txn.abort(); \
        this_thread::sleep_for(Random::duration_millis(3000)); \
        continue; \
      }\
      else if (e.code() == Error::HYPERSPACE_BERKELEYDB_REP_HANDLE_DEAD) { \
        txn_str << txn; \
        HT_INFOF("Berkeley DB rep handle dead deadlock encountered in txn %s", txn_str.str().c_str()); \
        txn.abort(); \
        continue; \
      }\
      else if (e.code() == Error::HYPERSPACE_BERKELEYDB_ERROR) \
        HT_ERROR_OUT << e << HT_END; \
      else \
        HT_ERRORF("%s - %s", Error::get_text(e.code()), e.what()); \
      txn.abort(); \
        return __VA_ARGS__; \
    } \
    HT_DEBUG_OUT << "end txn " << txn << HT_END; \
    break; \
  } while (true)

/*
 * Sets up the m_base_dir variable to be the absolute path of the root of the
 * Hyperspace directory (with no trailing slash); Locks this root directory to
 * prevent concurrent masters and then reads/increments/writes the 32-bit
 * integer attribute 'generation'; Creates the server Keepalive handler.
 */
Hyperspace::Master::Master(ConnectionManagerPtr &conn_mgr, PropertiesPtr &props,
               ServerKeepaliveHandlerPtr &keepalive_handler,
               ApplicationQueuePtr &app_queue_ptr)
  : m_next_handle_number(1), m_next_session_id(1),
    m_maintenance_outstanding(false),
    m_shutdown(false), m_bdb_fs(0) {

  m_lease_interval = props->get_ptr<gInt32t>("Hyperspace.Lease.Interval");
  m_verbose = props->get_ptr<gBool>("verbose");

  //m_keep_alive_interval = props->get_i32("Hyperspace.KeepAlive.Interval");
  //m_maintenance_interval = props->get_ptr<gInt32t>("Hyperspace.Maintenance.Interval");

  Path base_dir(props->get_str("Hyperspace.Replica.Dir"));

  if (!base_dir.is_complete()) {
    Path data_dir = props->get_str("Hypertable.DataDirectory");
    base_dir = data_dir / base_dir;
  }

  m_base_dir = base_dir.string();

  HT_INFOF("BerkeleyDB base directory = '%s'", m_base_dir.c_str());
  m_lock_file = m_base_dir + "/lock";

  // Make sure base directory exists, create if it doesn't
  if (!FileUtils::exists(m_base_dir.c_str())) {
    HT_INFOF("Base directory '%s' does not exist, creating...",
	     m_base_dir.c_str());
    if (!FileUtils::mkdirs(m_base_dir.c_str())) {
      HT_ERRORF("Unable to create base directory %s - %s",
                m_base_dir.c_str(), strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  if (!FileUtils::exists(m_lock_file.c_str())) {
    HT_INFOF("Lock file '%s' does not exist, creating...",
	     m_lock_file.c_str());
    if ((m_lock_fd = ::open(m_lock_file.c_str(), O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
      HT_ERRORF("Unable to create lock file '%s' - %s", m_lock_file.c_str(), strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
  else {
    if ((m_lock_fd = ::open(m_lock_file.c_str(), O_RDWR)) < 0) {
      HT_ERRORF("Unable to open lock file '%s' - %s", m_lock_file.c_str(), strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  /*
   * Lock the base directory to prevent concurrent masters
   */
#if defined(__sun__)
  struct flock fl;

  memset(&fl, 0, sizeof fl);

  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  fl.l_pid = getpid();

  if (fcntl(m_lock_fd, F_SETLKW, &fl) == -1) {
    if (errno == EWOULDBLOCK) {
      HT_ERRORF("Lock file '%s' is locked by another process.",
                m_lock_file.c_str());
    }
    else {
      HT_ERRORF("Unable to lock file '%s' - %s",
                m_lock_file.c_str(), strerror(errno));
    }
    exit(EXIT_FAILURE);
  }
#else
  if (flock(m_lock_fd, LOCK_EX | LOCK_NB) != 0) {
    if (errno == EWOULDBLOCK) {
      HT_ERRORF("Lock file '%s' is locked by another process.",
                m_lock_file.c_str());
    }
    else {
      HT_ERRORF("Unable to lock file '%s' - %s",
                m_lock_file.c_str(), strerror(errno));
    }
    exit(EXIT_FAILURE);
  }
#endif

  app_queue_ptr = make_shared<ApplicationQueue>(get_i32("workers"), false);
  vector<Thread::id> thread_ids = app_queue_ptr->get_thread_ids();
  thread_ids.push_back(ThisThread::get_id());


  m_bdb_fs = new BerkeleyDbFilesystem(props, m_base_dir, thread_ids);
  Event::set_bdb_fs(m_bdb_fs);

  /*
   * Load and increment generation number
   */
  if (is_master())
    get_generation_number();

  uint16_t port = props->get_i16("Hyperspace.Replica.Port");
  InetAddr::initialize(&m_local_addr, INADDR_ANY, port);

  m_metrics_handler = std::make_shared<MetricsHandler>(props);
  m_metrics_handler->start_collecting();

  m_last_tick = std::chrono::steady_clock::now();

  m_keepalive_handler_ptr = make_shared<ServerKeepaliveHandler>(conn_mgr->get_comm(), this, app_queue_ptr);
  m_keepalive_handler_ptr->start();
  keepalive_handler = m_keepalive_handler_ptr;
}


Hyperspace::Master::~Master() {
  m_metrics_handler->stop_collecting();
  delete m_bdb_fs;
  ::close(m_lock_fd);
}

/*
 * create_session does the following:
 * > Lock the session expiry map
 * > get and incr next session id from BDB
 * > create new session in BDB
 * > insert session id & expiry time into session expiry map
 */
uint64_t Hyperspace::Master::create_session(struct sockaddr_in &addr) {
  lock_guard<mutex> lock(m_session_map_mutex);

  SessionDataPtr session_data;
  uint64_t session_id = 0;
  String addr_str = InetAddr::format(addr);
  HT_INFOF("Create session for %s", addr_str.c_str());

  HT_BDBTXN_BEGIN() {
    // DB updates
    session_id = m_bdb_fs->get_next_id_i64(txn, SESSION, true);
    m_bdb_fs->create_session(txn, session_id, addr_str);
    // in mem updates
    session_data = make_shared<SessionData>(addr, m_lease_interval, session_id);
    m_session_map[session_id] = session_data;
    m_session_heap.push_back(session_data);

    txn.commit();
    HT_INFOF("created session %llu", (Llu)session_id);
  }
  HT_BDBTXN_END(0);

  return session_id;
}

/*
 *
 */
bool Hyperspace::Master::get_session(uint64_t session_id, SessionDataPtr &session_data) {
  lock_guard<mutex> lock(m_session_map_mutex);
  SessionMap::iterator iter = m_session_map.find(session_id);
  if (iter == m_session_map.end())
    return false;
  session_data = (*iter).second;
  return true;
}

/*
 * destroy_session does the following:
 * > Lock the session expiry map and erase the session data object from it
 * > Set the expiry time to now
 */
void Hyperspace::Master::destroy_session(uint64_t session_id) {
  lock_guard<mutex> lock(m_session_map_mutex);
  SessionDataPtr session_data;
  SessionMap::iterator iter = m_session_map.find(session_id);
  if (iter == m_session_map.end())
    return;
  session_data = (*iter).second;
  m_session_map.erase(session_id);
  session_data->expire();
  // force it to top of expiration heap
  session_data->set_expire_time_now();
  HT_INFOF("destroyed session %llu(%s)",
          (Llu)session_id, session_data->get_name());
}

/*
 *
 */
void Hyperspace::Master::initialize_session(uint64_t session_id, const String &name) {
  SessionDataPtr session_data;
  {
    lock_guard<mutex> lock(m_session_map_mutex);
    SessionMap::iterator iter = m_session_map.find(session_id);
    if (iter == m_session_map.end()) {
      HT_ERRORF("Unable to initialize session %llu (%s)", (Llu)session_id, name.c_str());
      return;
    }
    session_data = (*iter).second;
  }

  // set session name in BDB and mem
  HT_BDBTXN_BEGIN() {
    m_bdb_fs->set_session_name(txn, session_id, name);
    txn.commit();
    session_data->set_name(name);
  }
  HT_BDBTXN_END(BOOST_PP_EMPTY());

  HT_INFOF("Initialized session %llu (%s)", (Llu)session_id, name.c_str());
}

/*
 * renew_session_lease does the following:
 * > Lock the session expiry map
 * > If session lease can't be renewed
 *   > Do BDB txn to mark session as expired
 *   > Mark in mem session data as expired
 *   > (Don't delete session completely as handles etc need to be cleaned up)
 */
int Hyperspace::Master::renew_session_lease(uint64_t session_id) {
  lock_guard<mutex> lock(m_session_map_mutex);
  bool renewed = false;
  bool commited = false;
  SessionDataPtr session_data;

  SessionMap::iterator iter = m_session_map.find(session_id);
  if (iter == m_session_map.end())
    return Error::HYPERSPACE_EXPIRED_SESSION;

  session_data = iter->second;
  renewed = session_data->renew_lease();

  if (!renewed) {
    // if renew failed then delete from BDB
    HT_BDBTXN_BEGIN() {
      m_bdb_fs->expire_session(txn, session_id);
      txn.commit();
      commited = true;
    }
    HT_BDBTXN_END(Error::HYPERSPACE_EXPIRED_SESSION);

    // Do this outside BDB txn since delete event notifications might cause a BDB txn too
    if (commited)
      session_data->expire();

    // in mem session data will be cleaned up from map & heap in remove expired sessions
    return Error::HYPERSPACE_EXPIRED_SESSION;
  }

  return Error::OK;
}

/*
 * next_expired_session does the following:
 * > Lock the session map mutex
 * > Remake the session heap with the session with earliest expiry time on top
 * > If top of heap session is expired
 *   > Pop it from the heap, delete it from the session map
 *   > return true with session data parameter pointing to this session
 * > else return false
 */
bool
Hyperspace::Master::next_expired_session(SessionDataPtr &session_data,
                                         std::chrono::steady_clock::time_point now) {
  lock_guard<mutex> lock(m_session_map_mutex);
  struct LtSessionData ascending;

  if (!m_session_heap.empty()) {
    std::make_heap(m_session_heap.begin(), m_session_heap.end(), ascending);
    session_data = m_session_heap.front();
    if (session_data->is_expired(now) || m_shutdown) {
      m_session_heap.erase(m_session_heap.begin());
      m_session_map.erase(session_data->get_id());
      return true;
    }
  }
  session_data = 0;
  return false;
}


/*
 * remove_expired_sessions does the following:
 * > extend session expiry in case of suspension
 * > mark all expired sessions & get their open handles
 * > destroy all expired & open handles
 * > delete expired sessions in BDB
 */
void Hyperspace::Master::remove_expired_sessions() {
  SessionDataPtr session_data;
  int error;
  String errmsg;
  std::vector<uint64_t> handles;
  std::vector<uint64_t> expired_sessions;

  auto now = std::chrono::steady_clock::now();

  // mark expired sessions
  while (next_expired_session(session_data, now)) {
    bool commited = false;
    if (m_verbose->get())
      HT_INFOF("Expiring session %llu name=%s", (Llu)session_data->get_id(),
               session_data->get_name());
    commited = false;
    // expire session_data in mem and in BDB
    HT_BDBTXN_BEGIN() {
      m_bdb_fs->get_session_handles(txn, session_data->get_id(), handles);
      m_bdb_fs->expire_session(txn, session_data->get_id());
      txn.commit();
      commited = true;
      expired_sessions.push_back(session_data->get_id());
    }
    HT_BDBTXN_END(BOOST_PP_EMPTY());
    // keep this outside the BDB txn since
    if (commited)
      session_data->expire();
  }

  // delete handles open by expired sessions
  for (auto handle : handles) {
    if (m_verbose->get())
      HT_INFOF("Destroying handle %llu", (Llu)handle);
    if (!destroy_handle(handle, error, errmsg, false))
      HT_INFOF("Problem destroying handle - %s (%s)",
                Error::get_text(error), errmsg.c_str());
  }

  // delete expired sessions from BDB
  if (expired_sessions.size() > 0) {
    HT_BDBTXN_BEGIN() {
      for (auto expired_session : expired_sessions) {
        m_bdb_fs->delete_session(txn, expired_session);
      }
      txn.commit();
    }
    HT_BDBTXN_END(BOOST_PP_EMPTY());
  }
}

/*
 * Creates a directory with absolute path 'name'.
 *
 * Does the following:
 * > Find parent node
 * > Start BerkeleyDB txn to verify (and thus prevent modifications to) parent node,
 *     create new dir and
 * > Send out CHILD_NODE_ADDED notifications
 */
void
Hyperspace::Master::mkdir(ResponseCallback *cb, uint64_t session_id, const char *name, const std::vector<Attribute>& init_attrs) {
  bool commited = false;
  m_metrics_handler->request_increment();
  CommandContext ctx("mkdir", session_id);
  HT_BDBTXN_BEGIN() {
    commited = false;
    ctx.reset(&txn);
    mkdir(ctx, name);
    if (init_attrs.size() && !ctx.aborted)
      attr_set(ctx, 0, name, init_attrs);
    if (ctx.aborted)
      txn.abort();
    else {
      txn.commit();
      commited = true;
    }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    cb->error(ctx.error, ctx.error_msg);
    if (ctx.error == Error::HYPERSPACE_FILE_EXISTS) { // info should be sufficient
      HT_INFOF("%s - %s", Error::get_text(ctx.error), ctx.error_msg.c_str());
    }
    else {
        HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    }
    return;
  }

  // Deliver notifications if needed
  if (commited)
    deliver_event_notifications(ctx);

  if ((ctx.error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

void
Hyperspace::Master::mkdirs(ResponseCallback *cb, uint64_t session_id, const char *name, const std::vector<Attribute>& init_attrs) {
  std::vector<EventContext> evts;
  bool commited = false;
  m_metrics_handler->request_increment();
  CommandContext ctx("mkdirs", session_id);
  HT_BDBTXN_BEGIN() {
    commited = false;
    ctx.reset(&txn);
    bool file_exists;
    exists(ctx, name, file_exists);
    if (!ctx.aborted && !file_exists) {
      typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
      boost::char_separator<char> sep("/");
      std::vector<String> name_components;
      String path(name);
      tokenizer tokens(path, sep);
      for (tokenizer::iterator tok_iter = tokens.begin();
            tok_iter != tokens.end(); ++tok_iter)
        name_components.push_back(*tok_iter);

      path.clear();
      for (size_t i=0; i<name_components.size(); i++) {
        path += String("/") + name_components[i];
        mkdir(ctx, path.c_str());
        if (ctx.aborted && ctx.error != Error::HYPERSPACE_FILE_EXISTS)
          break;
        if (init_attrs.size() && !ctx.aborted &&
            i == name_components.size() - 1)
          attr_set(ctx, 0, name, init_attrs);
        ctx.reset_error();
      }
    }

    if (ctx.aborted)
      txn.abort();
    else {
      txn.commit();
      commited = true;
    }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    cb->error(ctx.error, ctx.error_msg);
    if (ctx.error == Error::HYPERSPACE_FILE_EXISTS) { // info should be sufficient
      HT_INFOF("%s - %s", Error::get_text(ctx.error), ctx.error_msg.c_str());
    }
    else {
        HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    }
    return;
  }

  // Deliver notifications if needed
  if (commited)
    deliver_event_notifications(ctx);

  if ((ctx.error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * Unlink
 *
 * Does the following:
 *
 * > Get the parent node
 * > Start BDB txn and validate (and so lock) the parent node
 * > Validate node to be deleted
 * > If there are no handles open for this node, create and persist node removed notifications
 * > Delete node from BDB fs and delete node data from BDB
 * > End BDB txn
 * > Deliver notifications
 */
void
Hyperspace::Master::unlink(ResponseCallback *cb, uint64_t session_id, const char *name) {
  bool commited = false;
  m_metrics_handler->request_increment();
  CommandContext ctx("unlink", session_id);
  HT_BDBTXN_BEGIN() {
    commited = false;
    ctx.reset(&txn);
    unlink(ctx, name);
    if (ctx.aborted)
      txn.abort();
    else {
      txn.commit();
      commited = true;
    }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  // Deliver notifications if needed
  if (commited)
    deliver_event_notifications(ctx);

  if ((ctx.error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}


/*
 * Open
 *
 */
void
Hyperspace::Master::open(ResponseCallbackOpen *cb, uint64_t session_id, const char *name,
    uint32_t flags, uint32_t event_mask, std::vector<Attribute> &init_attrs) {

  bool commited = false;
  uint64_t handle = 0;
  bool created = false;
  uint64_t lock_generation = 0;
  m_metrics_handler->request_increment();
  CommandContext ctx("open", session_id);
  HT_BDBTXN_BEGIN() {
    commited = false;
    handle = 0;
    created = false;
    lock_generation = 0;
    ctx.reset(&txn);
    open(ctx, name, flags, event_mask, init_attrs, handle, created, lock_generation);
    if (ctx.aborted)
      txn.abort();
    else {
      txn.commit();
      commited = true;
    }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  // Deliver notifications if needed
  if (commited)
    deliver_event_notifications(ctx);

  if (m_verbose->get())
    HT_INFOF("exitting open(session_id=%llu, session_name = %s, fname=%s, flags=0x%x, event_mask=0x%x)",
        (Llu)ctx.session_id, ctx.session_data->get_name(), name, flags, event_mask);

  if ((ctx.error = cb->response(handle, created, lock_generation)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}


/*
 * Close
 */
void Hyperspace::Master::close(ResponseCallback *cb, uint64_t session_id, uint64_t handle) {
  m_metrics_handler->request_increment();
  CommandContext ctx("close", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    close(ctx, handle);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  // if handle was open then destroy it (release lock if any, grant next
  // pending lock, delete ephemeral etc.)
  if (!destroy_handle(handle, ctx.error, ctx.error_msg)) {
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response_ok()) != Error::OK) {
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
  }
}

void Hyperspace::Master::close(CommandContext &ctx, uint64_t handle)
{
  if (!ctx.session_data) {
    if (!get_session(ctx.session_id, ctx.session_data)) {
      ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
      return;
    }
  }

  if (m_verbose->get())
    HT_INFOF("close(session=%llu(%s), handle=%llu)", (Llu)ctx.session_id,
             ctx.session_data->get_name(), (Llu)handle);

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  if (!m_bdb_fs->session_exists(txn, ctx.session_id)) {
    ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION,
                  format("Session %llu (%s) does not exist",
                         (Llu)ctx.session_id, ctx.session_data->get_name()));
    return;
  }

  m_bdb_fs->delete_session_handle(txn, ctx.session_id, handle);
}

/*
 * attr_set does the following:
 *
 * > Make sure session is valid
 * > Make sure handle is valid
 * > Start BDB txn
 *   > Lock node data
 *   > Set attribute in BDB
 *   > Persist ATTR_SET event notifications
 * > End BDB txn
 * > Deliver ATTR_SET notifications
 * > Send response
 */
void
Hyperspace::Master::attr_set(ResponseCallback *cb, uint64_t session_id, uint64_t handle,
                 const char *name, uint32_t oflags, const std::vector<Attribute> &attrs) {

  bool commited = false;
  uint64_t opened_handle = 0;
  m_metrics_handler->request_increment();
  CommandContext ctx("attrset", session_id);

  HT_ASSERT((name && *name) || handle);

  HT_BDBTXN_BEGIN() {
    commited = false;
    opened_handle = 0;
    ctx.reset(&txn);

    if (handle != 0)
      attr_set(ctx, handle, name, attrs);
    else {
      bool created;
      uint64_t lock_generation;
      std::vector<Attribute> none;
      open(ctx, name, oflags, 0, none, opened_handle, created, lock_generation);
      if (!ctx.aborted) {
        attr_set(ctx, opened_handle, 0, attrs);
        close(ctx, opened_handle);
      }
    }
    if (ctx.aborted)
      txn.abort();
    else {
      txn.commit();
      commited = true;
    }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  // deliver notifications
  if (commited)
    deliver_event_notifications(ctx);

  // if handle was open then destroy it (release lock if any, grant next
  // pending lock, delete ephemeral etc.)
  if (opened_handle) {
    if (!destroy_handle(opened_handle, ctx.error, ctx.error_msg)) {
      cb->error(ctx.error, ctx.error_msg);
      return;
    }
  }

  if ((ctx.error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * attr_get does the following:
 *
 * > Make sure session is valid
 * > Make sure handle is valid
 * > Start BDB txn
 *   > Lock node data
 *   > Get attribute in BDB
 * > End BDB txn
 * > Send attr value back in response
 *
 */
void
Hyperspace::Master::attr_get(ResponseCallbackAttrGet *cb, uint64_t session_id,
                 uint64_t handle, const char *name,
                 const std::vector<String> &attrs) {

  std::vector<DynamicBufferPtr> dbufs;
  dbufs.reserve(attrs.size());
  m_metrics_handler->request_increment();
  CommandContext ctx("attrget", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    dbufs.clear();
    if (attrs.size() == 1) { // if only one attr it might return HYPERSPACE_ATTR_NOT_FOUND
      dbufs.push_back(make_shared<DynamicBuffer>());
      attr_get(ctx, handle, name, attrs.front().c_str(), *dbufs.back());
    }
    else
      attr_get(ctx, handle, name, attrs, dbufs);

    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    if (ctx.error == Error::HYPERSPACE_ATTR_NOT_FOUND ||
        ctx.error == Error::HYPERSPACE_FILE_NOT_FOUND)
      HT_DEBUG_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    else
      HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(dbufs)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * attr_incr does the following:
 *
 * > Make sure session is valid
 * > Make sure handle is valid
 * > Start BDB txn
 *   > Lock node data
 *   > atomically increment attribute in BDB
 * > End BDB txn
 * > Send previous attr value back in response
 *
 */
void
Hyperspace::Master::attr_incr(ResponseCallbackAttrIncr *cb, uint64_t session_id,
                 uint64_t handle, const char *name, const char* attr) {

  uint64_t attr_val;
  m_metrics_handler->request_increment();
  CommandContext ctx("attrincr", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    attr_incr(ctx, handle, name, attr, attr_val);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(attr_val)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * attr_del does the following:
 *
 * > Make sure session is valid
 * > Make sure handle is valid
 * > Start BDB txn
 *   > Lock node data
 *   > Delete attribute in BDB
 *   > Deliver ATTR_DEL event notifications
 * > End BDB txn
 *
 */
void
Hyperspace::Master::attr_del(ResponseCallback *cb, uint64_t session_id, uint64_t handle,
                 const char *name) {
  bool commited = false;
  m_metrics_handler->request_increment();
  CommandContext ctx("attrdel", session_id);
  HT_BDBTXN_BEGIN() {
    commited = false;
    ctx.reset(&txn);
    attr_del(ctx, handle, name);
    if (ctx.aborted)
      txn.abort();
    else {
      txn.commit();
      commited = true;
    }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  // deliver notifications
  if (commited)
    deliver_event_notifications(ctx);

  if ((ctx.error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

void
Hyperspace::Master::attr_exists(ResponseCallbackAttrExists *cb, uint64_t session_id, uint64_t handle,
                    const char *name, const char *attr)
{
  bool exists = false;
  m_metrics_handler->request_increment();
  CommandContext ctx("attrexists", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    attr_exists(ctx, handle, name, attr, exists);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(exists)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

void
Hyperspace::Master::attr_list(ResponseCallbackAttrList *cb, uint64_t session_id, uint64_t handle)
{
  std::vector<String> attributes;
  m_metrics_handler->request_increment();
  CommandContext ctx("attrlist", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    attr_list(ctx, handle, attributes);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(attributes)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * exists does the following:
 *
 * > Start BDB txn
 *   > Check if file exists in BDB
 * > End BDB txn
 *
 */
void
Hyperspace::Master::exists(ResponseCallbackExists *cb, uint64_t session_id,
               const char *name) {

  bool file_exists = false;
  m_metrics_handler->request_increment();
  CommandContext ctx("exists", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    exists(ctx, name, file_exists);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(file_exists)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * read_dir does the following:
 *
 * > Make sure session is valid
 * > Start BDB txn
 *   > Make sure handle is valid
 *   > Read dir data from BDB
 * > End BDB txn
 *
 */
void
Hyperspace::Master::readdir(ResponseCallbackReaddir *cb, uint64_t session_id,
                uint64_t handle) {
  std::vector<DirEntry> listing;
  m_metrics_handler->request_increment();
  CommandContext ctx("readdir", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    readdir(ctx, handle, listing);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(listing)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * readdir_attr does the following:
 *
 * > Make sure session is valid
 * > Start BDB txn
 *   > Make sure handle is valid
 *   > Read dir data from BDB for entries in this dir that have the attr name set
 * > End BDB txn
 *
 */
void
Hyperspace::Master::readdir_attr(ResponseCallbackReaddirAttr *cb, uint64_t session_id,
                     uint64_t handle, const char *name, const char *attr, bool include_sub_entries) {
  std::vector<DirEntryAttr> listing;
  m_metrics_handler->request_increment();
  CommandContext ctx("readdirattr", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    readdir_attr(ctx, handle, name, attr, include_sub_entries, listing);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    if (ctx.error == Error::HYPERSPACE_FILE_NOT_FOUND)
      HT_DEBUG_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    else
      HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(listing)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * readpath_attr does the following:
 *
 * > Make sure session is valid
 * > Start BDB txn
 *   > Make sure handle is valid
 *   > Read the value of the specified attr for all path components in this file's name
 * > End BDB txn
 *
 */
void
Hyperspace::Master::readpath_attr(ResponseCallbackReadpathAttr *cb, uint64_t session_id,
                      uint64_t handle, const char *name, const char *attr) {
  std::vector<DirEntryAttr> listing;
  m_metrics_handler->request_increment();
  CommandContext ctx("readpathattr", session_id);
  HT_BDBTXN_BEGIN() {
    ctx.reset(&txn);
    readpath_attr(ctx, handle, name, attr, listing);
    if (ctx.aborted)
      txn.abort();
    else
      txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  if (ctx.aborted) {
    if (ctx.error == Error::HYPERSPACE_FILE_NOT_FOUND)
      HT_DEBUG_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    else
      HT_ERROR_OUT << Error::get_text(ctx.error) << " - " << ctx.error_msg << HT_END;
    cb->error(ctx.error, ctx.error_msg);
    return;
  }

  if ((ctx.error = cb->response(listing)) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(ctx.error));
}

/*
 * shutdown
 */
void Hyperspace::Master::shutdown(ResponseCallback *cb, uint64_t session_id) {
  if (m_verbose->get())
    HT_INFOF("shutdown(session=%llu", (Llu)session_id);

  // destroy session
  destroy_session(session_id);

  // destroy dangling sessions...
  {
    lock_guard<mutex> lock(m_session_map_mutex);
    m_shutdown = true;
    SessionDataPtr session_data;
    while (m_session_map.size()) {
      SessionMap::iterator iter = m_session_map.begin();
      session_data = (*iter).second;
      m_session_map.erase(iter);
      session_data->expire();
      // force it to top of expiration heap
      session_data->set_expire_time_now();
      HT_INFOF("destroyed dangling session %llu(%s)",
              (Llu)session_data->get_id(), session_data->get_name());
    }
  }

  //...and remove
  remove_expired_sessions();

  int error;
  if ((error = cb->response_ok()) != Error::OK)
    HT_ERRORF("Problem sending back response - %s", Error::get_text(error));

  m_keepalive_handler_ptr->shutdown();
}

void Hyperspace::Master::status(ResponseCallbackStatus *cb) {
  HT_INFO("status");
  cb->response(m_status);
}

/*
 * lock
 */
void
Hyperspace::Master::lock(ResponseCallbackLock *cb, uint64_t session_id, uint64_t handle,
             uint32_t mode, bool try_lock) {
  SessionDataPtr session_data;
  bool notify = true;
  uint32_t open_flags, cur_lock_mode;
  String node;
  uint64_t lock_generation = 0;
  uint64_t event_id;
  HyperspaceEventPtr lock_acquired_event;
  NotificationMap lock_acquired_notifications;
  bool persisted_notifications = false;
  bool aborted=false, commited=false;
  int lock_status = 0;
  int error = Error::OK;
  String error_msg;

  if (!get_session(session_id, session_data)) {
    cb->error(Error::HYPERSPACE_EXPIRED_SESSION, "");
    return;
  }

  if (m_verbose->get()) {
    HT_INFOF("lock(session=%llu(%s), handle=%llu, mode=0x%x, try_lock=%d)",
             (Llu)session_id, session_data->get_name(), (Llu)handle, mode, try_lock);
  }

  HT_BDBTXN_BEGIN() {
    // (re) initialize vars
    aborted = false; commited = false; persisted_notifications = false;
    lock_status=0;

    // make sure session is still valid
    if (!m_bdb_fs->session_exists(txn, session_id)) {
      error = Error::HYPERSPACE_EXPIRED_SESSION;
      error_msg = format("session: %lld", (Lld)session_id);
      aborted = true;
      goto txn_commit;
    }

    if (!m_bdb_fs->handle_exists(txn, handle)) {
      aborted = true;
      goto txn_commit;
    }

    open_flags = m_bdb_fs->get_handle_open_flags(txn, handle);
    if (!(open_flags & OPEN_FLAG_LOCK)) {
      error = Error::HYPERSPACE_MODE_RESTRICTION;
      error_msg = "handle not open for locking";
      aborted = true;
      goto txn_commit;
    }

    if (!(open_flags & OPEN_FLAG_WRITE)) {
      error = Error::HYPERSPACE_MODE_RESTRICTION;
      error_msg = "handle not open for writing";
      aborted = true;
      goto txn_commit;
    }

    m_bdb_fs->get_handle_node(txn, handle, node);
    cur_lock_mode = m_bdb_fs->get_node_cur_lock_mode(txn, node);
    if (cur_lock_mode == LOCK_MODE_EXCLUSIVE) {
      if (try_lock)
        lock_status = LOCK_STATUS_BUSY;
      else {
        // don't abort transaction since we need to persist pending lock req
        LockRequest lock_request(handle, mode);
        m_bdb_fs->add_node_pending_lock_request(txn, node, lock_request);
        lock_status = LOCK_STATUS_PENDING;
      }
      goto txn_commit;
    }
    else if (cur_lock_mode == LOCK_MODE_SHARED) {
      if (mode == LOCK_MODE_EXCLUSIVE) {
        if (try_lock)
          lock_status = LOCK_STATUS_BUSY;
        else {
          // don't abort transaction since we need to persist pending lock req
          LockRequest lock_request(handle, mode);
          m_bdb_fs->add_node_pending_lock_request(txn, node, lock_request);
          lock_status = LOCK_STATUS_PENDING;
        }
        goto txn_commit;
      }

      HT_ASSERT(mode == LOCK_MODE_SHARED);

      if (m_bdb_fs->node_has_pending_lock_request(txn, node)) {
        if (try_lock)
          lock_status = LOCK_STATUS_BUSY;
        else {
          // don't abort transaction since we need to persist pending lock req
          LockRequest lock_request(handle, mode);
          m_bdb_fs->add_node_pending_lock_request(txn, node, lock_request);
          lock_status = LOCK_STATUS_PENDING;
        }
        goto txn_commit;
      }
    }

    // at this point we're OK to acquire the lock
    if (mode == LOCK_MODE_SHARED && m_bdb_fs->node_has_shared_lock_handles(txn, node))
      notify = false;

    lock_status = LOCK_STATUS_GRANTED;
    lock_generation = m_bdb_fs->incr_node_lock_generation(txn, node);

    m_bdb_fs->set_xattr_i64(txn, node, "lock.generation", lock_generation);
    m_bdb_fs->set_node_cur_lock_mode(txn, node, mode);
    lock_handle(txn, handle, mode, node);

    // create lock acquired event & persist event notifications
    if (notify) {
      event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);
      m_bdb_fs->create_event(txn, EVENT_TYPE_LOCK_ACQUIRED, event_id, EVENT_MASK_LOCK_ACQUIRED,
                             mode);
      lock_acquired_event = make_shared<EventLockAcquired>(event_id, mode);
      if (m_bdb_fs->get_node_event_notification_map(txn, node, EVENT_MASK_LOCK_ACQUIRED,
                                                    lock_acquired_notifications)) {
        persist_event_notifications(txn, event_id, lock_acquired_notifications);
        persisted_notifications = true;
      }
    }

    // commit the txn
    txn_commit:
      if (aborted) {
        txn.abort();
        std::stringstream sout;
        sout << "lock txn=" << txn << " aborted " << " handle=" << handle << " node="
             << node << " mode=" << mode << " status=" << lock_status
             << " lock_generation=" << lock_generation;
        HT_INFOF("%s", sout.str().c_str());
      }
      else {
        txn.commit();
        commited = true;
        std::stringstream sout;
        sout << "lock txn=" << txn << " commited " << " handle=" << handle << " node="
             << node << " mode=" << mode << " status=" << lock_status
             << " lock_generation=" << lock_generation << " notification_count="
             << lock_acquired_notifications.size();
        HT_INFOF("%s", sout.str().c_str());
      }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (aborted) {
    cb->error(error, error_msg);
    return;
  }

  // send lock request response
  switch (lock_status) {
    case LOCK_STATUS_GRANTED:
      cb->response(lock_status, lock_generation);
      break;
    default:
      cb->response(lock_status);
  }

  // deliver lock acquired event notifications
  if (commited && persisted_notifications) {
    deliver_event_notifications(lock_acquired_event, lock_acquired_notifications);
  }

}

/*
 * Assumes node is locked and BDB txn has started.
 * lock_handle does the following:
 * > If node name string is null then fill in the node name
 * > Set exclusive handle to acquiring handle or add acquiring handle to set of shared handles
 * > Set handle data to locked
 */
void Hyperspace::Master::lock_handle(BDbTxn &txn, uint64_t handle, uint32_t mode, String& node) {

  if (node == "")
    m_bdb_fs->get_handle_node(txn, handle, node);

  if (mode == LOCK_MODE_SHARED)
    m_bdb_fs->add_node_shared_lock_handle(txn, node, handle);
  else {
    HT_ASSERT(mode == LOCK_MODE_EXCLUSIVE);
    m_bdb_fs->set_node_exclusive_lock_handle(txn, node, handle);
  }
  m_bdb_fs->set_handle_locked(txn, handle, true);
}

/*
 * Assumes node is locked.
 * lock_handle does the following:
 * > Set exclusive handle to acquiring handle or add acquiring handle to set of shared handles
 * > Set handle data to locked
 */
void Hyperspace::Master::lock_handle(BDbTxn &txn, uint64_t handle, uint32_t mode, const String& node) {

  HT_ASSERT(node != "");
  if (mode == LOCK_MODE_SHARED)
    m_bdb_fs->add_node_shared_lock_handle(txn, node, handle);
  else {
    HT_ASSERT(mode == LOCK_MODE_EXCLUSIVE);
    m_bdb_fs->set_node_exclusive_lock_handle(txn, node, handle);
  }
  m_bdb_fs->set_handle_locked(txn, handle, true);
}


/*
 * release
 */
void
Hyperspace::Master::release(ResponseCallback *cb, uint64_t session_id, uint64_t handle) {
  SessionDataPtr session_data;
  String node;
  int error = 0;
  String error_msg;
  NotificationMap lock_release_notifications, lock_granted_notifications,
                  lock_acquired_notifications;
  HyperspaceEventPtr lock_release_event, lock_granted_event, lock_acquired_event;
  bool aborted = false, commited = false;

  if (!get_session(session_id, session_data)) {
    cb->error(Error::HYPERSPACE_EXPIRED_SESSION, "");
    return;
  }

  if (m_verbose->get()) {
    HT_INFOF("release(session=%llu(%s), handle=%llu)",
             (Llu)session_id, session_data->get_name(), (Llu)handle);
  }

  // txn 1: release lock
  HT_BDBTXN_BEGIN() {

    // make sure session is still valid
    if (!m_bdb_fs->session_exists(txn, session_id)) {
      error = Error::HYPERSPACE_EXPIRED_SESSION;
      error_msg = format("session: %lld", (Lld)session_id);
      aborted = true;
      goto txn_commit_1;
    }

    if (!m_bdb_fs->handle_exists(txn, handle)) {
      error = Error::HYPERSPACE_INVALID_HANDLE;
      error_msg = format("handle=%lld", (Lld)handle);
      aborted = true;
      goto txn_commit_1;
    }

    m_bdb_fs->get_handle_node(txn, handle, node);

    release_lock(txn, handle, node, lock_release_event, lock_release_notifications);

    txn_commit_1:
      if (aborted)
        txn.abort();
      else {
        txn.commit();
        commited = true;
      }
  }
  HT_BDBTXN_END_CB(cb);

  // check for errors
  if (aborted) {
    HT_ERROR_OUT << Error::get_text(error) << " - " << error_msg << HT_END;
    cb->error(error, error_msg);
    return;
  }

  // deliver lock released notifications
  if (commited)
    deliver_event_notifications(lock_release_event, lock_release_notifications);

  // txn 2: grant pending lock(s)
  HT_BDBTXN_BEGIN() {
    grant_pending_lock_reqs(txn, node, lock_granted_event, lock_granted_notifications,
        lock_acquired_event, lock_acquired_notifications);
    txn.commit();
  }
  HT_BDBTXN_END_CB(cb);

  // deliver lock granted & acquired notifications
  deliver_event_notifications(lock_granted_event, lock_granted_notifications);
  deliver_event_notifications(lock_acquired_event, lock_acquired_notifications);

  cb->response_ok();
}

/*
 * release_lock: does the following
 * > if handle is not locked return
 * > if lock is exclusive
 *   > set node exclusive lock handle to 0
 * > else
 *   > delete this shared lock handle from node
 * > set handle to unlocked
 * > if node has no shared lock handles (at this point it has no exclusive lock handle)
 *   > create a new LOCK_RELEASED event and persist in BDB
 *   > get map of handles -> sessions to be notified of LOCK_RELEASE event on this node
 *   > persist LOCK_RELEASE notifications
 *   > set node to unlocked in BDB
 *
 */
void Hyperspace::Master::release_lock(BDbTxn &txn, uint64_t handle, const String &node,
    HyperspaceEventPtr &release_event, NotificationMap &release_notifications) {
  vector<uint64_t> next_lock_handles;
  uint64_t exclusive_lock_handle=0;

  if (m_bdb_fs->handle_is_locked(txn, handle)) {
    exclusive_lock_handle = m_bdb_fs->get_node_exclusive_lock_handle(txn,node);
    if (exclusive_lock_handle != 0) {
      HT_ASSERT(handle == exclusive_lock_handle);
      m_bdb_fs->set_node_exclusive_lock_handle(txn, node, 0);
    }
    else {
      m_bdb_fs->delete_node_shared_lock_handle(txn, node, handle);
    }
    m_bdb_fs->set_handle_locked(txn, handle, false);
  }
  else
    return;

  // persist LOCK_RELEASED notifications if no more locks held on node
  if (!m_bdb_fs->node_has_shared_lock_handles(txn, node)) {
    HT_INFO("Persisting lock released notifications");
    uint64_t event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);
    release_event = make_shared<EventLockReleased>(event_id);
    m_bdb_fs->create_event(txn, EVENT_TYPE_LOCK_RELEASED, event_id,
                           release_event->get_mask());
    if (m_bdb_fs->get_node_event_notification_map(txn, node, release_event->get_mask(),
                                                  release_notifications)) {
      persist_event_notifications(txn, event_id, release_notifications);
    }

    m_bdb_fs->set_node_cur_lock_mode(txn, node, 0);
    HT_INFO("Finished persisting lock released notifications");
  }
}

/*
 * grant_pending_lock_reqs does the following
 *
 * > Check if the node has pending locks
 * > Get the first pending exclusive lock or set of next pending shared locks
 * > Grant lock to next pending lock(s)
 * > Persist lock granted notifications
 * > Persist lock acquired notifications
 */
void Hyperspace::Master::grant_pending_lock_reqs(BDbTxn &txn, const String &node,
    HyperspaceEventPtr &lock_granted_event, NotificationMap &lock_granted_notifications,
    HyperspaceEventPtr &lock_acquired_event, NotificationMap &lock_acquired_notifications) {
  vector<uint64_t> next_lock_handles;
  int next_mode = 0;
  LockRequest front_lock_req;

  if (m_bdb_fs->get_node_pending_lock_request(txn, node, front_lock_req)) {
    next_mode = front_lock_req.mode;

    if (next_mode == LOCK_MODE_EXCLUSIVE) {
      // get the pending exclusive lock request
      next_lock_handles.push_back(front_lock_req.handle);
      m_bdb_fs->delete_node_pending_lock_request(txn, node, front_lock_req.handle);
    }
    else {
      // gather up all the pending shared lock requests preceeding the next exclusive request
      HT_ASSERT(next_mode == LOCK_MODE_SHARED);
      LockRequest lockreq = front_lock_req;
      do {
        if (lockreq.mode != LOCK_MODE_SHARED)
          break;
        next_lock_handles.push_back(lockreq.handle);
        m_bdb_fs->delete_node_pending_lock_request(txn, node, lockreq.handle);
      } while (m_bdb_fs->get_node_pending_lock_request(txn, node, lockreq));
    }

    if (!next_lock_handles.empty()) {
      // we have at least 1 pending lock request
      // grant lock to next pending locks and persist lock granted notifications
      uint64_t lock_generation = m_bdb_fs->incr_node_lock_generation(txn, node);
      uint64_t event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);
      uint64_t session;
      m_bdb_fs->create_event(txn, EVENT_TYPE_LOCK_GRANTED, event_id, EVENT_MASK_LOCK_GRANTED,
                             next_mode, lock_generation);
      m_bdb_fs->set_xattr_i64(txn, node, "lock.generation", lock_generation);
      m_bdb_fs->set_node_cur_lock_mode(txn, node, next_mode);

      lock_granted_event = make_shared<EventLockGranted>(event_id, next_mode, lock_generation);

      for (auto handle : next_lock_handles) {
        lock_handle(txn, handle, next_mode, node);
        session = m_bdb_fs->get_handle_session(txn, handle);
        lock_granted_notifications[handle] = session;
      }
      // persist lock granted notifications
      persist_event_notifications(txn, event_id, lock_granted_notifications);

      // create lock acquired event
      event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);
      m_bdb_fs->create_event(txn, EVENT_TYPE_LOCK_ACQUIRED, event_id, EVENT_MASK_LOCK_ACQUIRED,
                             next_mode);
      lock_acquired_event = make_shared<EventLockAcquired>(event_id, next_mode);
      // persist lock acquired notifications
      if (m_bdb_fs->get_node_event_notification_map(txn, node, EVENT_MASK_LOCK_ACQUIRED,
                                                    lock_acquired_notifications))
        persist_event_notifications(txn, event_id, lock_acquired_notifications);
    }
  }
}

/*
 * Assumes it is in the middle of a BDB txn
 *
 * > Store the handles affected by an event in BerkeleyDB
 */
void
Hyperspace::Master::persist_event_notifications(BDbTxn &txn, uint64_t event_id,
                                    NotificationMap &handles_to_sessions)
{
  if (handles_to_sessions.size() > 0) {
    vector<uint64_t> handles;
    for (NotificationMap::iterator iter = handles_to_sessions.begin();
         iter != handles_to_sessions.end(); iter++) {
      handles.push_back(iter->first);
    }
    m_bdb_fs->set_event_notification_handles(txn, event_id, handles);
  }
}

/*
 * Assumes it is in the middle of a BDB txn
 *
 * > Store the handle addected by an event in BerkeleyDB
 */
void
Hyperspace::Master::persist_event_notifications(BDbTxn &txn, uint64_t event_id, uint64_t handle)
{
  vector<uint64_t> handles;
  handles.push_back(handle);
  m_bdb_fs->set_event_notification_handles(txn, event_id, handles);
}

/*
 *
 */
void
Hyperspace::Master::deliver_event_notifications(HyperspaceEventPtr &event_ptr,
    NotificationMap &handles_to_sessions, bool wait_for_notify)
{
  SessionDataPtr session_data;
  uint64_t session_id;
  uint64_t handle_id;
  bool has_notifications = false;
  vector<uint64_t> sessions;

  for (NotificationMap::iterator iter = handles_to_sessions.begin();
       iter != handles_to_sessions.end(); iter++) {
    handle_id = iter->first;
    session_id = iter->second;
    if(get_session(session_id,session_data)) {
      session_data->add_notification(new Notification(handle_id, event_ptr ) );
      sessions.push_back(session_id);
      has_notifications = true;
    }
  }

  if (has_notifications) {
    String sessions_str;

    for (auto session_id : sessions) {
      m_keepalive_handler_ptr->deliver_event_notifications(session_id);
      sessions_str += String(" ") + session_id;
    }

    if (wait_for_notify)
      event_ptr->wait_for_notifications();

    if (m_verbose->get())
      HT_INFOF("exitting deliver_event_notifications for event_id=%llu mask=0x%x sessions=(%s )",
               (Llu)event_ptr->get_id(), (int)(Llu)event_ptr->get_mask(), sessions_str.c_str());
  }
  else {
    HT_DEBUG_OUT << "exitting deliver_event_notifications nothing to do"<< HT_END;
  }
}

/*
 *
 */
bool
Hyperspace::Master::find_parent_node(const String &normal_name,String &parent_name, String &child_name) {
  size_t last_slash = normal_name.rfind("/", normal_name.length());

  child_name.clear();

  if (last_slash > 0) {
    parent_name = normal_name.substr(0, last_slash);
    child_name.append(normal_name, last_slash + 1, normal_name.length() - last_slash - 1);
    return true;
  }
  else if (last_slash == 0) {
    parent_name = "/";
    child_name.append(normal_name, 1, normal_name.length() - 1);
    return true;
  }

  return false;
}

/*
 * destroy_handle does the following:
 * > Start BDB txn
 * > Read in Node data
 * > Call release lock passing it the parent txn (update handle and node to unlocked,
 *       remove handle from node data, grant lock to next acquiring handles,
 *       persist LOCK_RELEASED, GRANTED and ACQUIRED notifications)
 * > Keep track of node id and delete handle data from BDB
 * > End BDB txn
 * > Deliver notifications
 *
 * > If no one else has this node open
 *   > if this is an ephemeral node
 *     > Start BDB txn
 *       > persist CHILD_NODE_REMOVED event notification
 *       > delete node from BDB
 *     > End BDB txn
 *
 */
bool
Hyperspace::Master::destroy_handle(uint64_t handle, int &error, String &errmsg,
                       bool wait_for_notify) {
  bool has_refs = false;
  NotificationMap lock_release_notifications, lock_granted_notifications,
                  lock_acquired_notifications, node_removed_notifications;
  HyperspaceEventPtr lock_release_event, lock_granted_event, lock_acquired_event,
                     node_removed_event ;
  bool node_removed = false;
  String node;
  bool aborted = false;

  HT_DEBUG_OUT << "destroy_handle (handle=" << handle << ")" << HT_END;

  // txn 1: release lock
  HT_BDBTXN_BEGIN() {
    // Make sure handle is valid is not being deleted by someone else
    if (!m_bdb_fs->handle_exists(txn, handle) ||
        m_bdb_fs->get_handle_del_state(txn, handle) != HANDLE_NOT_DEL) {
      aborted = true;
      error = Error::HYPERSPACE_INVALID_HANDLE;
      errmsg = format("Handle %lld already deleted or being deleted", (Lld)handle);
      goto txn_commit;
    }
    m_bdb_fs->set_handle_del_state(txn, handle, HANDLE_MARKED_FOR_DEL);
    m_bdb_fs->get_handle_node(txn, handle, node);
    m_bdb_fs->delete_node_handle(txn, node, handle);
    release_lock(txn, handle, node, lock_release_event, lock_release_notifications);

    txn_commit:
      txn.commit();
  }
  HT_BDBTXN_END(false);

  if (aborted)
    return false;

  // deliver lock released notifications
  deliver_event_notifications(lock_release_event, lock_release_notifications,
                              wait_for_notify);

  // txn 2: grant pending lock(s)
  HT_BDBTXN_BEGIN() {
    grant_pending_lock_reqs(txn, node, lock_granted_event, lock_granted_notifications,
        lock_acquired_event, lock_acquired_notifications);
    txn.commit();
  }
  HT_BDBTXN_END(false);

  // deliver lock granted & acquired notifications
  deliver_event_notifications(lock_granted_event, lock_granted_notifications, wait_for_notify);
  deliver_event_notifications(lock_acquired_event, lock_acquired_notifications,
                              wait_for_notify);

  // txn 3: delete node if ephemeral and no one has it open
  HT_BDBTXN_BEGIN() {
    has_refs = m_bdb_fs->node_has_open_handles(txn, node);
    if (!has_refs && m_bdb_fs->node_is_ephemeral(txn, node)) {
      String parent_node, child_node;

      if (find_parent_node(node, parent_node, child_node)) {
        // persist child node removed notifications
        uint64_t event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);
        m_bdb_fs->create_event(txn, EVENT_TYPE_NAMED, event_id,
                               EVENT_MASK_CHILD_NODE_REMOVED, child_node);
        node_removed_event = make_shared<EventNamed>(event_id, EVENT_MASK_CHILD_NODE_REMOVED,
                                                     child_node);
        if (m_bdb_fs->get_node_event_notification_map(txn, parent_node,
            EVENT_MASK_CHILD_NODE_REMOVED, node_removed_notifications)) {
          persist_event_notifications(txn, event_id, node_removed_notifications);
        }
        // unlink file and delete node data from BDB
        m_bdb_fs->unlink(txn, node);
        m_bdb_fs->delete_node(txn, node);
        node_removed = true;
      }
    }
    txn.commit();
  }
  HT_BDBTXN_END(false);
  // deliver node removed notifications
  if (node_removed) {
    deliver_event_notifications(node_removed_event, node_removed_notifications,
                                wait_for_notify);
  }

  // txn 4: delete handle data from BDB
  HT_BDBTXN_BEGIN() {
    m_bdb_fs->delete_handle(txn, handle);
    txn.commit();
  }
  HT_BDBTXN_END(false);

  return true;
}

void Hyperspace::Master::handle_sleep() {
  m_sleep_time = std::chrono::steady_clock::now();
  HT_INFO("Suspend detected.");
}


void Hyperspace::Master::handle_wakeup() {

  auto now = std::chrono::steady_clock::now();

  std::chrono::milliseconds lease_credit = 
    std::chrono::duration_cast<std::chrono::milliseconds>(now - m_sleep_time) +
    std::chrono::milliseconds(m_lease_interval->get());

  // extend all leases
  {
    lock_guard<mutex> lock(m_session_map_mutex);
    if (!m_shutdown) {
      HT_INFOF("Resume detected, extending all session leases "
               "by %lu milliseconds.", (Lu)lease_credit.count());
      for (SessionMap::iterator iter = m_session_map.begin();
           iter != m_session_map.end(); iter++)
        (*iter).second->extend_lease(lease_credit);
    }
  }
}

void Hyperspace::Master::do_maintenance() {

  {
    lock_guard<mutex> lock(m_maintenance_mutex);
    if (m_maintenance_outstanding)
      return;
    m_maintenance_outstanding = true;
  }

  m_bdb_fs->do_checkpoint();

  {
    lock_guard<mutex> lock(m_maintenance_mutex);
    m_maintenance_outstanding = false;
  }

}

void Hyperspace::Master::mkdir(CommandContext &ctx, const char *name) {
  if (m_verbose->get()) {
    HT_INFOF("%s(session_id=%llu, name=%s)", ctx.friendly_name, (Llu)ctx.session_id, name);
  }

  String parent_node, child_name;
  if (!find_parent_node(name, parent_node, child_name) || strlen(name)==0) {
    ctx.set_error(Error::HYPERSPACE_FILE_EXISTS, "directory '/' exists");
    return;
  }

  if (name[0] != '/' || name[strlen(name)-1] == '/') {
    ctx.set_error(Error::HYPERSPACE_BAD_PATHNAME, (String)"directory '" + name + "' bad");
    return;
  }

  if (!ctx.session_data) {
    if (!get_session(ctx.session_id, ctx.session_data)) {
      ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
      return;
    }
  }

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  // make sure parent node data is setup
  if (!validate_and_create_node_data(txn, parent_node)) {
    ctx.set_error(Error::HYPERSPACE_FILE_NOT_FOUND, (String)"' parent node: '" + parent_node + "'");
    return;
  }

  // make sure this node doesn't exist already
  if (m_bdb_fs->exists(txn, name)) {
    ctx.set_error(Error::HYPERSPACE_FILE_EXISTS, (String)"node: '" + name + "'");
    return;
  }

  // create event and persist notifications
  create_event(ctx, parent_node, EVENT_MASK_CHILD_NODE_ADDED, child_name);

  // create node
  m_bdb_fs->mkdir(txn, name);

  // create node data
  m_bdb_fs->create_node(txn, name, false, 1);
}



void Hyperspace::Master::open(CommandContext &ctx, const char *name,
          uint32_t flags, uint32_t event_mask,
          std::vector<Attribute> &init_attrs, uint64_t& handle,
          bool& created, uint64_t& lock_generation) {

  handle = 0;
  created = false;
  lock_generation = 0;

  String child_name, node = name, parent_node;
  bool lock_notify = false;
  uint32_t lock_mode = 0;

  HT_ASSERT(name[0] == '/');

  if (!ctx.session_data) {
    if (!get_session(ctx.session_id, ctx.session_data)) {
      ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
      return;
    }
  }

  if (m_verbose->get()) {
    HT_INFOF("open(session_id=%llu, session_name = %s, fname=%s, flags=0x%x, event_mask=0x%x)",
             (Llu)ctx.session_id,ctx. session_data->get_name(), name, flags, event_mask);
  }

  if (!find_parent_node(name, parent_node, child_name)) {
    ctx.set_error(Error::HYPERSPACE_BAD_PATHNAME, name);
    return;
  }

  if (!init_attrs.empty() && !(flags & OPEN_FLAG_CREATE)) {
    ctx.set_error(Error::HYPERSPACE_CREATE_FAILED, "initial attributes can only be supplied on CREATE");
    return;
  }

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  // make sure session is still valid
  if (!m_bdb_fs->session_exists(txn, ctx.session_id)) {
    ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
    return;
  }

  // make sure parent node is valid and create node_data if needed
  if (!validate_and_create_node_data(txn, parent_node)) {
    ctx.set_error(Error::HYPERSPACE_FILE_NOT_FOUND, (String)" node: '" + node + "' parent node: '" + parent_node + "'");
    return;
  }

  bool existed = m_bdb_fs->exists(txn, name);
  if (existed) { // node exists in DB already
    // check flags
    if ((flags & OPEN_FLAG_CREATE) && (flags & OPEN_FLAG_EXCL)) {
      ctx.set_error(Error::HYPERSPACE_FILE_EXISTS, "mode=CREATE|EXCL");
      return;
    }

    if ((flags & OPEN_FLAG_TEMP)) {
      ctx.set_error(Error::HYPERSPACE_FILE_EXISTS, (String) "Unable to open TEMP file " + node + "because it already exists");
      return;
    }

    // create node data if it doesn't exist and set lock generation
    validate_and_create_node_data(txn, node);

    // check for lock mode conflicts
    if (flags & (OPEN_FLAG_LOCK_SHARED|OPEN_FLAG_LOCK_EXCLUSIVE)) {
      uint32_t cur_lock_mode = m_bdb_fs->get_node_cur_lock_mode(txn, node);
      if ((flags & OPEN_FLAG_LOCK_SHARED) == OPEN_FLAG_LOCK_SHARED) {
        if (cur_lock_mode == LOCK_MODE_EXCLUSIVE) {
          ctx.set_error(Error::HYPERSPACE_LOCK_CONFLICT, node);
          return;
        }
        lock_mode = LOCK_MODE_SHARED;
        if (!m_bdb_fs->node_has_shared_lock_handles(txn, node))
          lock_notify = true;
      }
      else if ((flags & OPEN_FLAG_LOCK_EXCLUSIVE) == OPEN_FLAG_LOCK_EXCLUSIVE) {
        if (cur_lock_mode == LOCK_MODE_SHARED || cur_lock_mode == LOCK_MODE_EXCLUSIVE) {
          ctx.set_error(Error::HYPERSPACE_LOCK_CONFLICT, node);
          return;
        }
        lock_mode = LOCK_MODE_EXCLUSIVE;
        lock_notify = true;
      }
    }
  } // node exists in DB already
  else { // node doesn't exist in DB
    if (!(flags & OPEN_FLAG_CREATE)) {
      ctx.set_error(Error::HYPERSPACE_FILE_NOT_FOUND, name);
      return;
    }
    // create new node
    lock_generation = 1;
    m_bdb_fs->create(txn, name, (flags & OPEN_FLAG_TEMP) > 0);
    m_bdb_fs->set_xattr_i64(txn, name, "lock.generation",
                            lock_generation);

    // create a new node data object in hyperspace
    m_bdb_fs->create_node(txn, node, (flags & OPEN_FLAG_TEMP) > 0, lock_generation);

    // Set the initial attributes
    for (size_t i=0; i<init_attrs.size(); i++)
      m_bdb_fs->set_xattr(txn, name, init_attrs[i].name,
                          init_attrs[i].value, init_attrs[i].value_len);
    created = true;
  } // node doesn't exist in DB
  handle = m_bdb_fs->get_next_id_i64(txn, HANDLE, true);
  m_bdb_fs->create_handle(txn, handle, node, flags, event_mask, ctx.session_id, false,
                          HANDLE_NOT_DEL);
  m_bdb_fs->add_session_handle(txn, ctx.session_id, handle);

  // create node added event and persist notifications
  create_event(ctx, parent_node, EVENT_MASK_CHILD_NODE_ADDED, child_name);

  /*
    * If open flags LOCK_SHARED or LOCK_EXCLUSIVE, then obtain lock
    */
  if (lock_mode != 0) {
    lock_generation = m_bdb_fs->incr_node_lock_generation(txn, node);
    m_bdb_fs->set_xattr_i64(txn, name, "lock.generation",
                            lock_generation);

    m_bdb_fs->set_node_cur_lock_mode(txn, node, lock_mode);
    lock_handle(txn, handle, lock_mode, node);

    // create and persist lock acquired event
    // deliver notification to handles to this same node
    if (lock_notify) {
      uint64_t lock_acquired_event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);

      m_bdb_fs->create_event(txn, EVENT_TYPE_LOCK_ACQUIRED, lock_acquired_event_id,
                              EVENT_MASK_LOCK_ACQUIRED, lock_mode);

      std::vector<EventContext>::iterator it = ctx.evts.insert(ctx.evts.end(),
          EventContext(make_shared<EventLockAcquired>(lock_acquired_event_id, lock_mode)));

      if (m_bdb_fs->get_node_event_notification_map(txn, node, EVENT_MASK_LOCK_ACQUIRED,
                                                it->notifications)) {
        persist_event_notifications(txn, lock_acquired_event_id,
                                    it->notifications);
        it->persisted_notifications = true;
      }
    }
  }

  m_bdb_fs->add_node_handle(txn, node, handle);

  HT_INFOF("handle %llu created ('%s', session=%llu(%s), flags=0x%x, mask=0x%x)",
            (Llu)handle, node.c_str(), (Llu)ctx.session_id, ctx.session_data->get_name(),
            flags, event_mask);
}

void Hyperspace::Master::unlink(CommandContext &ctx, const char *name) {
  if (m_verbose->get()) {
    HT_INFOF("%s(session_id=%llu, name=%s)", ctx.friendly_name, (Llu)ctx.session_id, name);
  }

  if (!strcmp(name, "/")) {
    ctx.set_error(Error::HYPERSPACE_PERMISSION_DENIED,
              "Cannot remove '/' directory");
    return;
  }

  if (!ctx.session_data) {
    if (!get_session(ctx.session_id, ctx.session_data)) {
      ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
      return;
    }
  }

  String node=name;
  String child_name, parent_node;
  if (!find_parent_node(node, parent_node, child_name)) {
    ctx.set_error(Error::HYPERSPACE_BAD_PATHNAME, name);
    return;
  }

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  // make sure parent node data is setup
  if (!validate_and_create_node_data(txn, parent_node)) {
    ctx.set_error(Error::HYPERSPACE_FILE_NOT_FOUND, (String)" node: '" + node);
    return;
  }

  // make sure file & node data exist
  if (!validate_and_create_node_data(txn, node)) {
    ctx.set_error(Error::HYPERSPACE_FILE_NOT_FOUND, node);
    return;
  }

  bool has_refs = m_bdb_fs->node_has_open_handles(txn, node);
  if (has_refs) {
    ctx.set_error(Error::HYPERSPACE_FILE_OPEN, "File is still open and referred to by some handle");
    return;
  }

  // Sanity check
  HT_ASSERT(name[0] == '/' && name[strlen(name)-1] != '/');

  // Create event and persist notifications
  create_event(ctx, parent_node, EVENT_MASK_CHILD_NODE_REMOVED, child_name);

  // Delete node
  m_bdb_fs->unlink(txn, name);

  // Delete node data
  m_bdb_fs->delete_node(txn, node);
}

void Hyperspace::Master::attr_set(CommandContext &ctx, uint64_t handle,
                      const char *name, const std::vector<Attribute> &attrs) {
  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  std::string attr_names;
  size_t total_value_len = 0;
  if (m_verbose->get()) {
    for (const auto &attr : attrs) {
      attr_names += attr.name;
      attr_names += ",";
      total_value_len += attr.value_len;
    }
    boost::trim_right_if(attr_names, boost::is_any_of(","));
  }

  String node;
  if (name && *name) {
    if (!get_named_node(ctx, name, attr_names.c_str(), node))
      return;
  }
  else
    if (!get_handle_node(ctx, handle, attr_names.c_str(), node))
      return;

  for (const auto &attr : attrs) {
    m_bdb_fs->set_xattr(txn, node, attr.name, attr.value, attr.value_len);
    // create event notification and persist
    create_event(ctx, node, EVENT_MASK_ATTR_SET, attr.name);
  }

  if (m_verbose->get()) {
    HT_INFOF("exitting attrset(session=%llu(%s), handle=%llu, name=%s, value_len=%d)",
             (Llu)ctx.session_id, ctx.session_data->get_name(), (Llu)handle, attr_names.c_str(), (int)total_value_len);
  }
}

void Hyperspace::Master::attr_get(CommandContext &ctx, uint64_t handle,
                      const char *name, const char *attr, DynamicBuffer &dbuf) {
  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (name && *name) {
    if (!get_named_node(ctx, name, attr, node))
      return;
  }
  else
    if (!get_handle_node(ctx, handle, attr, node))
      return;

  if (!m_bdb_fs->get_xattr(txn, node, attr, dbuf)) {
    ctx.set_error(Error::HYPERSPACE_ATTR_NOT_FOUND, attr);
    return;
  }
}

void Hyperspace::Master::attr_get(CommandContext &ctx, uint64_t handle,
                      const char *name, const std::vector<String> &attrs,
                      std::vector<DynamicBufferPtr> &dbufs) {
  dbufs.clear();
  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (name && *name) {
    if (!get_named_node(ctx, name, 0, node))
      return;
  }
  else
    if (!get_handle_node(ctx, handle, 0, node))
      return;

  dbufs.reserve(attrs.size());
  for (const auto &attr : attrs) {
    dbufs.push_back(make_shared<DynamicBuffer>());
    if (!m_bdb_fs->get_xattr(txn, node, attr, *dbufs.back()))
      dbufs.back() = 0; // attr not found
  }
}

void Hyperspace::Master::attr_incr(CommandContext &ctx, uint64_t handle,
                       const char *name, const char* attr, uint64_t& attr_val) {
  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (name && *name) {
    if (!get_named_node(ctx, name, attr, node))
      return;
  }
  else
    if (!get_handle_node(ctx, handle, attr, node))
      return;

  if (!m_bdb_fs->incr_attr(txn, node, attr, &attr_val)) {
    ctx.set_error(Error::HYPERSPACE_ATTR_NOT_FOUND, attr);
    return;
  }
}

void Hyperspace::Master::attr_del(CommandContext &ctx, uint64_t handle, const char *name) {
  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (!get_handle_node(ctx, handle, name, node))
    return;
  m_bdb_fs->del_xattr(txn, node, name);

  // create event notification and persist
  create_event(ctx, node, EVENT_MASK_ATTR_DEL, name);
}

void Hyperspace::Master::attr_exists(CommandContext& ctx, uint64_t handle,
                         const char *name, const char *attr, bool& exists) {
  exists = false;

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (name && *name) {
    if (!get_named_node(ctx, name, attr, node))
      return;
  }
  else
    if (!get_handle_node(ctx, handle, attr, node))
      return;

  if (m_bdb_fs->exists_xattr(txn, node, attr))
    exists = true;
}

void Hyperspace::Master::attr_list(CommandContext& ctx, uint64_t handle, std::vector<String>& attributes) {
  attributes.clear();

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (!get_handle_node(ctx, handle, 0, node))
    return;

  if (!m_bdb_fs->list_xattr(txn, node, attributes)) {
    ctx.set_error(Error::HYPERSPACE_ATTR_NOT_FOUND,
                  format("handle=%lld node=%s", (Lld)handle, node.c_str()));
    return;
  }
}

void Hyperspace::Master::exists(CommandContext& ctx, const char *name, bool& file_exists) {
  file_exists = false;

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  if (m_verbose->get())
    HT_INFOF("exists(session_id=%llu, name=%s)", (Llu)ctx.session_id, name);

  HT_ASSERT(name[0] == '/' && (name[1] == '\0' || name[strlen(name)-1] != '/'));
  file_exists = m_bdb_fs->exists(txn, name);

  if (m_verbose->get())
    HT_INFOF("exitting exists(session_id=%llu, name=%s)", (Llu)ctx.session_id, name);
}

void Hyperspace::Master::readdir(CommandContext& ctx, uint64_t handle, std::vector<DirEntry>& listing) {
  listing.clear();

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (!get_handle_node(ctx, handle, 0, node))
    return;

  m_bdb_fs->get_directory_listing(txn, node, listing);
}

void Hyperspace::Master::readdir_attr(CommandContext& ctx, uint64_t handle, const char *name, const char *attr,
                          bool include_sub_entries, std::vector<DirEntryAttr>& listing) {
  listing.clear();

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  if (name && *name) {
    if (!get_named_node(ctx, name, attr, node))
      return;
  }
  else
    if (!get_handle_node(ctx, handle, attr, node))
      return;

  m_bdb_fs->get_directory_attr_listing(txn, node, attr, include_sub_entries, listing);
}

void Hyperspace::Master::readpath_attr(CommandContext& ctx, uint64_t handle, const char *name, const char *attr,
                           std::vector<DirEntryAttr>& listing) {
  listing.clear();

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  String node;
  bool node_is_dir;
  if (name && *name) {
    if (!get_named_node(ctx, name, attr, node, &node_is_dir))
      return;
  }
  else {
    if (!get_handle_node(ctx, handle, attr, node))
      return;
    m_bdb_fs->exists(txn, node, &node_is_dir);
  }

  size_t pos = 0;
  String path_component;
  DynamicBuffer attr_buf;
  DirEntryAttr entry;

  // iterate over all path components and get attribute value if present
  while (pos != string::npos) {
    pos = node.find('/', pos);
    entry.is_dir = true;

    if (pos == string::npos)
      entry.is_dir = node_is_dir;
    else
      pos++;

    path_component = node.substr(0, pos);
    entry.name = path_component;

    // insert entry to result list if it has the attribute
    if (m_bdb_fs->get_xattr(txn, path_component, attr, attr_buf)) {
      entry.attr = attr_buf;
      entry.has_attr = true;
    }
    else {
      entry.attr.free();
      entry.has_attr = false;
    }
    listing.push_back(entry);
  }
}

/*
 * Validates the session and returns the node for the handle specified
 */
bool Hyperspace::Master::get_handle_node(CommandContext &ctx, uint64_t handle, const char* attr, String &node) {
  if (!ctx.session_data) {
    if (!get_session(ctx.session_id, ctx.session_data)) {
      ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
      return false;
    }
  }

  if (m_verbose->get()) {
    if (attr && *attr)
      HT_INFOF("%s(session=%llu(%s), handle=%llu, attr=%s)", ctx.friendly_name,
               (Llu)ctx.session_id, ctx.session_data->get_name(), (Llu)handle, attr);
    else
      HT_INFOF("%s(session=%llu(%s), handle=%llu)", ctx.friendly_name,
               (Llu)ctx.session_id, ctx.session_data->get_name(), (Llu)handle);
  }

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  // make sure session is still valid
  if (!m_bdb_fs->session_exists(txn, ctx.session_id)) {
    ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
    return false;
  }

  if (!m_bdb_fs->handle_exists(txn, handle)) {
    ctx.set_error(Error::HYPERSPACE_INVALID_HANDLE,
                  format("Session %llu, handle=%llu", (Llu)ctx.session_id, (Llu)handle));
    return false;
  }

  m_bdb_fs->get_handle_node(txn, handle, node);
  return true;
}

/*
 * Validates the session and returns the node for the name specified
 */
bool Hyperspace::Master::get_named_node(CommandContext &ctx, const char *name, const char* attr, String &node, bool *is_dir) {
  if (!ctx.session_data) {
    if (!get_session(ctx.session_id, ctx.session_data)) {
      ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
      return false;
    }
  }

  if (m_verbose->get()) {
    if (attr && *attr)
      HT_INFOF("%s(session=%llu(%s), name=%s, attr=%s)", ctx.friendly_name,
                 (Llu)ctx.session_id, ctx.session_data->get_name(), name, attr);
    else
      HT_INFOF("%s(session=%llu(%s), name=%s)", ctx.friendly_name,
                 (Llu)ctx.session_id, ctx.session_data->get_name(), name);
  }

  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  node = name;
  String child_name, parent_node;
  if (!find_parent_node(node, parent_node, child_name)) {
    ctx.set_error(Error::HYPERSPACE_BAD_PATHNAME, name);
    return false;
  }
  boost::trim_right_if(node, boost::is_any_of("/"));

  // make sure node exists
  if (!m_bdb_fs->exists(txn, node, is_dir)) {
    ctx.set_error(Error::HYPERSPACE_FILE_NOT_FOUND, (String)"node: '" + name + "'");
    return false;
  }

  // make sure session is still valid
  if (!m_bdb_fs->session_exists(txn, ctx.session_id)) {
    ctx.set_error(Error::HYPERSPACE_EXPIRED_SESSION, format("Session %llu", (Llu)ctx.session_id));
    return false;
  }
  return true;
}

/*
 */
void Hyperspace::Master::create_event(CommandContext &ctx, const String &node, uint32_t event_mask, const String &name) {
  HT_ASSERT(ctx.txn);
  BDbTxn &txn = *ctx.txn;

  uint64_t event_id = m_bdb_fs->get_next_id_i64(txn, EVENT, true);
  m_bdb_fs->create_event(txn, EVENT_TYPE_NAMED, event_id, event_mask, name);

  std::vector<EventContext>::iterator it = ctx.evts.insert(ctx.evts.end(),
       EventContext(make_shared<EventNamed>(event_id, event_mask, name)));

  if (m_bdb_fs->get_node_event_notification_map(txn, node, event_mask,
                                                it->notifications)) {
    persist_event_notifications(txn, event_id, it->notifications);
    it->persisted_notifications = true;
  }
  else
    it->persisted_notifications = false;
}

/*
 */
void Hyperspace::Master::deliver_event_notifications(CommandContext &ctx, bool wait_for_notify) {
  for (auto &evt : ctx.evts)
    if (evt.persisted_notifications)
      deliver_event_notifications(evt, wait_for_notify);
}

void Hyperspace::Master::deliver_event_notifications(EventContext &evt, bool wait_for_notify) {
  deliver_event_notifications(evt.event, evt.notifications, wait_for_notify);
}

/*
 */
void Hyperspace::Master::get_generation_number() {

  HT_BDBTXN_BEGIN() {
    if (!m_bdb_fs->get_xattr_i32(txn, "/hyperspace/metadata", "generation",
                                 &m_generation))
      m_generation = 0;
    m_generation++;
    m_bdb_fs->set_xattr_i32(txn, "/hyperspace/metadata", "generation",
                            m_generation);
    txn.commit();
  }
  HT_BDBTXN_END(BOOST_PP_EMPTY());
}

/*
 * If node exists but corresponding node data doesn't then
 * create the node data
 */
bool
Hyperspace::Master::validate_and_create_node_data(BDbTxn &txn, const String &node)
{
  // make sure node is exists
  if (!m_bdb_fs->exists(txn, node)) {
    return false;
  }

  // create node data for this node and set its lock generation
  if (!m_bdb_fs->node_exists(txn, node)) {
    uint64_t lock_generation;
    if (!m_bdb_fs->get_xattr_i64(txn, node, "lock.generation", &lock_generation)) {
      lock_generation = 1;
      m_bdb_fs->set_xattr_i64(txn, node, "lock.generation", lock_generation);
    }
    m_bdb_fs->create_node(txn, node, false, lock_generation);
  }

  return true;
}
