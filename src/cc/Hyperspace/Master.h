/* -*- c++ -*-
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

#ifndef Hyperspace_Master_h
#define Hyperspace_Master_h

#include <Hyperspace/BerkeleyDbFilesystem.h>
#include <Hyperspace/MetricsHandler.h>
#include <Hyperspace/Protocol.h>
#include <Hyperspace/ServerKeepaliveHandler.h>
#include <Hyperspace/response/ResponseCallbackAttrExists.h>
#include <Hyperspace/response/ResponseCallbackAttrGet.h>
#include <Hyperspace/response/ResponseCallbackAttrIncr.h>
#include <Hyperspace/response/ResponseCallbackAttrList.h>
#include <Hyperspace/response/ResponseCallbackExists.h>
#include <Hyperspace/response/ResponseCallbackLock.h>
#include <Hyperspace/response/ResponseCallbackOpen.h>
#include <Hyperspace/response/ResponseCallbackReaddir.h>
#include <Hyperspace/response/ResponseCallbackReaddirAttr.h>
#include <Hyperspace/response/ResponseCallbackReadpathAttr.h>
#include <Hyperspace/response/ResponseCallbackStatus.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>
#include <AsyncComm/Event.h>
#include <AsyncComm/ResponseCallback.h>

#include <Common/Properties.h>
#include <Common/SockAddrMap.h>
#include <Common/Status.h>
#include <Common/StringExt.h>
#include <Common/Time.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Hyperspace {

  class Master {
  public:

    enum { TIMER_INTERVAL_MS=1000 };

    /*
     * Enumerate handle deletion states
     * TODO: Add more states as part of Hyperspace state recovery. Hence don't use bool now
     */
    enum {
      HANDLE_NOT_DEL = 1,
      HANDLE_MARKED_FOR_DEL = 2
    };

    Master(ConnectionManagerPtr &, PropertiesPtr &, ServerKeepaliveHandlerPtr &,
           ApplicationQueuePtr &app_queue_ptr);
    ~Master();
    bool is_master() {
      if (m_bdb_fs)
        return m_bdb_fs->is_master();
      else
        return false;
    }
    String get_current_master() {
      if (m_bdb_fs)
        return m_bdb_fs->get_current_master();
      else
        return (String) "";
    };
    // Hyperspace command implementations
    void mkdir(ResponseCallback *cb, uint64_t session_id, const char *name, const std::vector<Attribute>& init_attrs);
    void mkdirs(ResponseCallback *cb, uint64_t session_id, const char *name, const std::vector<Attribute>& init_attrs);
    void unlink(ResponseCallback *cb, uint64_t session_id, const char *name);
    void open(ResponseCallbackOpen *cb, uint64_t session_id, const char *name,
              uint32_t flags, uint32_t event_mask,
              std::vector<Attribute> &init_attrs);
    void close(ResponseCallback *cb, uint64_t session_id, uint64_t handle);
    void attr_set(ResponseCallback *cb, uint64_t session_id, uint64_t handle,
                  const char *name, uint32_t oflags, const std::vector<Attribute> &attrs);
    void attr_get(ResponseCallbackAttrGet *cb, uint64_t session_id,
                  uint64_t handle, const char *name, const std::vector<String> &attrs);
    void attr_incr(ResponseCallbackAttrIncr *cb, uint64_t session_id,
                   uint64_t handle, const char *name, const char *attr);
    void attr_del(ResponseCallback *cb, uint64_t session_id, uint64_t handle,
                  const char *name);
    void attr_exists(ResponseCallbackAttrExists *cb, uint64_t session_id, uint64_t handle,
                     const char *name, const char *attr);
    void attr_list(ResponseCallbackAttrList *cb,
                   uint64_t session_id, uint64_t handle);
    void exists(ResponseCallbackExists *cb, uint64_t session_id,
                const char *name);
    void readdir(ResponseCallbackReaddir *cb, uint64_t session_id,
                 uint64_t handle);
    void readdir_attr(ResponseCallbackReaddirAttr *cb, uint64_t session_id,
                      uint64_t handle, const char *name, const char *attr,
                      bool include_sub_entries);
    void readpath_attr(ResponseCallbackReadpathAttr *cb, uint64_t session_id,
                       uint64_t handle, const char *name, const char *attr);
    void shutdown(ResponseCallback *cb, uint64_t session_id);
    void status(ResponseCallbackStatus *cb);
    void lock(ResponseCallbackLock *cb, uint64_t session_id, uint64_t handle,
              uint32_t mode, bool try_lock);
    void release(ResponseCallback *cb, uint64_t session_id, uint64_t handle);

    /*
     * Creates a new session by allocating a new SessionData object, obtaining a
     * new session ID and inserting the object into the Session map.
     *
     * @param addr Address from which the UDP client is sending
     * @return the new session ID
     */
    uint64_t create_session(struct sockaddr_in &addr);

    /*
     * Obtains the SessionData object for the given id from the session map.
     *
     * @param session_id Session ID to lookup
     * @param session_data Reference to SessionData smart pointer
     * @return true if found, false otherwise
     */
    bool get_session(uint64_t session_id, SessionDataPtr &session_data);

    void destroy_session(uint64_t session_id);
    void initialize_session(uint64_t session_id, const String &name);

    /*
     * Attempts to renew the session lease for session with the given ID.  If
     * the session cannot be found or if it is expired, the method returns
     * Error::HYPERSPACE_EXPIRED_SESSION otherwise, it renews the session
     * lease by invoking the RenewLease method of the SessionData object.
     *
     * @param session_id Session ID to renew
     * @return Error::OK if successful
     */
    int renew_session_lease(uint64_t session_id);

    bool next_expired_session(SessionDataPtr &,
                              std::chrono::steady_clock::time_point now);
    void remove_expired_sessions();


    void get_datagram_send_address(struct sockaddr_in *addr) {
      memcpy(addr, &m_local_addr, sizeof(m_local_addr));
    }

    void tick() {
      std::lock_guard<std::mutex> lock(m_last_tick_mutex);
      m_last_tick = std::chrono::steady_clock::now();
    }

    /// Handle sleep event (e.g. laptop close).
    /// This method works in conjunction with handle_wakeup() to allow
    /// Hyperspace to continue working after the machine on which it is running
    /// has been suspended and resumed, for example when the laptop is closed
    /// and then reopened.  It records the current time in #m_sleep_time, which
    /// is then used by handle_wakeup() to extend session leases.
    void handle_sleep();

    /// Handle wakeup event (e.g. laptop open).
    /// This method works in conjunction with handle_sleep() to allow
    /// Hyperspace to continue working after the machine on which it is running
    /// has been suspended and resumed, for example when the laptop is closed
    /// and then reopened.  It extends all session leases by an amount computed
    /// as follows:
    /// <pre>
    /// (now - m_sleep_time) + lease_interval
    /// </pre>
    void handle_wakeup();

    void do_maintenance();

  private:

    struct EventContext {
      HyperspaceEventPtr event;
      NotificationMap notifications;
      bool persisted_notifications;

      EventContext()
        : persisted_notifications(false) {}

      EventContext(HyperspaceEventPtr _event)
        : event(_event), persisted_notifications(false) {}

      void reset() {
        event = 0;
        notifications.clear();
        persisted_notifications = false;
      }
    };

    struct CommandContext {
      const char* friendly_name;
      uint64_t session_id;
      SessionDataPtr session_data;
      BDbTxn *txn;
      std::vector<EventContext> evts;
      bool aborted;
      int error;
      String error_msg;

      CommandContext(const char* _friendly_name, uint64_t _session_id)
        : friendly_name(_friendly_name), session_id(_session_id), txn(0),
          aborted(false), error(Error::OK) {}

      void set_error(int _error, const char *_error_msg, bool abort=true) {
        error = _error;
        error_msg = _error_msg;
        aborted = abort;
      }

      void set_error(int _error, const String &_error_msg, bool abort=true) {
        error = _error;
        error_msg = _error_msg;
        aborted = abort;
      }

      void reset_error() {
        aborted = false;
        error = Error::OK;
        error_msg.clear();
      }

      void reset(BDbTxn *_txn) {
        session_data = 0;
        txn = _txn;
        evts.clear();
        reset_error();
      }
    };

    void mkdir(CommandContext &ctx, const char *name);
    void unlink(CommandContext &ctx, const char *name);
    void open(CommandContext &ctx, const char *name,
              uint32_t flags, uint32_t event_mask,
              std::vector<Attribute> &init_attrs, uint64_t& handle,
              bool& created, uint64_t& lock_generation);
    void close(CommandContext &ctx, uint64_t handle);
    void attr_set(CommandContext &ctx, uint64_t handle,
                  const char *name, const std::vector<Attribute> &attrs);
    void attr_get(CommandContext &ctx, uint64_t handle,
                  const char *name, const char *attr, DynamicBuffer &dbuf);
    void attr_get(CommandContext &ctx, uint64_t handle,
                  const char *name, const std::vector<String> &attrs,
                  std::vector<DynamicBufferPtr> &dbufs);
    void attr_incr(CommandContext &ctx, uint64_t handle,
                   const char *name, const char *attr, uint64_t& attr_val);
    void attr_del(CommandContext &ctx, uint64_t handle, const char *name);
    void attr_exists(CommandContext& ctx, uint64_t handle,
                     const char *name, const char *attr, bool& exists);
    void attr_list(CommandContext& ctx, uint64_t handle, std::vector<String>& attributes);
    void exists(CommandContext& ctx, const char *name, bool& file_exists);
    void readdir(CommandContext& ctx, uint64_t handle, std::vector<DirEntry>& listing);
    void readdir_attr(CommandContext& ctx, uint64_t handle, const char *name, const char *attr,
                      bool include_sub_entries, std::vector<DirEntryAttr>& listing);
    void readpath_attr(CommandContext& ctx, uint64_t handle, const char *name, const char *attr,
                       std::vector<DirEntryAttr>& listing);
    bool get_handle_node(CommandContext &ctx, uint64_t handle, const char* attr, String &node);
    bool get_named_node(CommandContext &ctx, const char *name, const char* attr, String &node, bool *is_dir=0);
    void create_event(CommandContext &ctx, const String &node,
                      uint32_t event_mask, const String &name);
    void deliver_event_notifications(CommandContext &ctx, bool wait_for_notify = true);
    void deliver_event_notifications(EventContext &evt, bool wait_for_notify = true);

    void get_generation_number();

    void normalize_name(std::string name, std::string &normal);
    void deliver_event_notifications(HyperspaceEventPtr &event_ptr,
        NotificationMap &handles_to_sessions, bool wait_for_notify = true);
    void persist_event_notifications(BDbTxn &txn, uint64_t event_id,
                                     NotificationMap &handles_to_sessions);
    void persist_event_notifications(BDbTxn &txn, uint64_t event_id, uint64_t handle);
    bool validate_and_create_node_data(BDbTxn &txn, const String &node);
    /*
     * Locates the parent 'node' of the given pathname.  It determines the name
     * of the parent node by stripping off the characters incuding and after
     * the last '/' character.  It then looks up the name in the m_node_map and
     * sets the pointer reference <code>parent_name</code> to it. As a side effect,
     * it also saves the child name (e.g. characters after the last '/' character
     * to the string reference child_name.  NOTE: This method locks the
     * m_node_map_mutex.
     *
     * @param normal_name Normalized (e.g. no trailing '/') name of path to
     *        find parent of
     * @param parent_name reference to string to hold return parent name
     * @param child_name reference to string to hold the child directory entry
     *        name
     * @return true if parent node found, false otherwise
     */
    bool find_parent_node(const std::string &normal_name,
                          std::string &parent_name, std::string &child_name);
    bool destroy_handle(uint64_t handle, int &error, String &errmsg,
                        bool wait_for_notify=true);
    void release_lock(BDbTxn &txn, uint64_t handle, const String &node,
        HyperspaceEventPtr &release_event, NotificationMap &release_notifications);
    void lock_handle(BDbTxn &txn, uint64_t handle, uint32_t mode, String &node);
    void lock_handle(BDbTxn &txn, uint64_t handle, uint32_t mode, const String &node);
    void lock_handle_with_notification(uint64_t handle, uint32_t mode,
                                       bool wait_for_notify=true);
    void grant_pending_lock_reqs(BDbTxn &txn, const String &node,
        HyperspaceEventPtr &lock_granted_event, NotificationMap &lock_granted_notifications,
        HyperspaceEventPtr &lock_acquired_event, NotificationMap &lock_acquired_notifications);

    typedef std::vector<SessionDataPtr> SessionDataVec;
    typedef std::unordered_map<uint64_t, SessionDataPtr> SessionMap;

    gBoolPtr      m_verbose;
    gInt32tPtr    m_lease_interval;
    // uint32_t      m_keep_alive_interval;
    // gInt32t       m_maintenance_interval;
    std::string   m_base_dir;
    std::string   m_lock_file;
    int           m_lock_fd;
    uint32_t      m_generation;
    uint64_t      m_next_handle_number;
    uint64_t      m_next_session_id;
    ServerKeepaliveHandlerPtr m_keepalive_handler_ptr;
    struct sockaddr_in m_local_addr;
    SessionDataVec m_session_heap;
    SessionMap m_session_map;
    MetricsHandlerPtr m_metrics_handler;

    std::mutex m_session_map_mutex;
    std::mutex m_last_tick_mutex;
    std::mutex m_maintenance_mutex;
    bool m_maintenance_outstanding {};
    std::chrono::steady_clock::time_point m_last_tick;

    /// Suspension time recorded by handle_sleep()
    std::chrono::steady_clock::time_point m_sleep_time;

    bool m_shutdown {};

    // BerkeleyDB state
    BerkeleyDbFilesystem *m_bdb_fs;

    /// Program status tracker
    Status m_status;

  };

  typedef std::shared_ptr<Master> MasterPtr;

} // namespace Hyperspace

#endif // Hyperspace_Master_h
