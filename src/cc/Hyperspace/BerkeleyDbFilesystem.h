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

/** @file
 * Declarations for BerkeleyDbFilesystem.
 * This file contains declarations for BerkeleyDbFilesystem, a class that
 * implements the Hyperspace filesystem on top of BerkeleyDB.
 */

#ifndef Hyperspace_BerkeleyDbFilesystem_h
#define Hyperspace_BerkeleyDbFilesystem_h

#include <Common/Compat.h>

#include <Hyperspace/DirEntry.h>
#include <Hyperspace/DirEntryAttr.h>
#include <Hyperspace/StateDbKeys.h>

#include <Common/DynamicBuffer.h>
#include <Common/FileUtils.h>
#include <Common/InetAddr.h>
#include <Common/InetAddr.h>
#include <Common/Properties.h>
#include <Common/String.h>
#include <Common/StringExt.h>
#include <Common/Thread.h>

#include <db_cxx.h>

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <unordered_map>
#include <vector>

namespace Hyperspace {

  /** @addtogroup Hyperspace
   * @{
   */

  /// Hash map from Node handle ID to Session ID
  typedef std::unordered_map<uint64_t, uint64_t> NotificationMap;

  /** Encapsulates a lock request for a file node. */
  class LockRequest {
  public:
    /** Constructor.
     * @param h Node handle ID
     * @param m Lock mode
     */
    LockRequest(uint64_t h=0, int m=0) : handle(h), mode(m) { return; }
    /// Node handle ID
    uint64_t handle;
    /// Lock mode
    uint32_t mode;
  };

  /** Enumeration for object identifier types */
  enum IdentifierType {
    SESSION = 0, /**< Session identifier */
    HANDLE,      /**< Handle identifier */
    EVENT        /**< Event identifier */
  };

  /** Encapsulates replication state. */
  class ReplicationInfo {
  public:
    /** Constructor. */
    ReplicationInfo() : do_replication(true) { }

    /** Waits for master election to finish. */
    void wait_for_election() {
      std::unique_lock<std::mutex> lock(m_election_mutex);
      m_election_cond.wait(lock, [this](){ return m_election_done; });
    }

    /** Finish master election. */
    void finish_election() {
      std::lock_guard<std::mutex> lock(m_election_mutex);
      if (!m_election_done) {
        m_election_done = true;
        m_election_cond.notify_all();
      }
    }

    /** Check if master election is finished.
     * @return <i>true</i> if master election has finished, <i>false</i> otherwise
     */
    bool election_finished() {
      std::lock_guard<std::mutex> lock(m_election_mutex);
      return m_election_done;
    }

    bool do_replication {};
    bool is_master {};
    int master_eid {-1};
    uint32_t num_replicas {};
    String localhost;
    std::unordered_map<int, String> replica_map;

  private:

    /// %Mutex for serializing access to #m_election_done
    std::mutex m_election_mutex;

    /// Condition variable for signaling change to #m_election_done
    std::condition_variable m_election_cond;

    /// Indicates if master election has finished
    bool m_election_done {};
  };

  /** Manages <i>namespace</i> and transient <i>state</i> database handles */
  class BDbHandles {
  public:

    /** Constructor. */
    BDbHandles(): open(false), handle_namespace_db(0), handle_state_db(0) {}

    /** Destructor. Calls close(). */
    ~BDbHandles() {
      close();
    }

    /** Closes and destroys database handles
     * Closes and destroys #handle_namespace_db and #handle_state_db and sets
     * #open to <i>false</i>.
     */
    void close() {
      if (open) {
        try {
          handle_namespace_db->close(0);
          handle_state_db->close(0);
          delete handle_namespace_db;
          delete handle_state_db;
          handle_namespace_db = 0;
          handle_state_db = 0;
          open = false;
        }
        catch(DbException &e) {
          HT_ERROR_OUT << "Error closing Berkeley DB handles" << HT_END;
        }
      }
    }

    /// Indicates if database handles have been opened
    bool open;

    /// Database handle for persistent filesystem namespace
    Db *handle_namespace_db;

    /// Database handle transient state
    Db *handle_state_db;
  };

  /// Smart pointer to BDbHandles
  typedef std::shared_ptr<BDbHandles> BDbHandlesPtr;

  /** Manages transaction state */
  class BDbTxn {
  public:
    /** Constructor. */
    BDbTxn(): handle_namespace_db(0), handle_state_db(0), db_txn(0) {}

    /** Commit transaction.
     * @param flag BerkeleyDB commit flags
     */
    void commit(int flag=0) {
      db_txn->commit(flag);
      db_txn = 0;
    }

    /** Abort transaction. */
    void abort() {
      db_txn->abort();
      db_txn = 0;
    }

    /// Filesystem namespace database handle
    Db *handle_namespace_db;

    /// Transient state database handle
    Db *handle_state_db;

    /// BerkeleyDB transaction object
    DbTxn *db_txn;
  };

  /** Writes human-readable version of <code>txn</code> to an ostream.
   * @param out Output stream on which to write
   * @param txn Transaction object to display
   * @return <code>out</code>
   */
  std::ostream &operator<<(std::ostream &out, const BDbTxn &txn);

  /** Hyperspace filesystem implementation on top of BerkeleyDB.
   */
  class BerkeleyDbFilesystem {
  public:

    /** Constructor.
     * @param props Configruation properties
     * @param basedir Directory of BerkeleyDB database files
     * @param thread_ids Vector of application thread IDs
     * @param force_recover Force catastrophic recovery logic
     */
    BerkeleyDbFilesystem(PropertiesPtr &props,
                         const String &basedir,
                         const std::vector<Thread::id> &thread_ids,
                         bool force_recover=false);

    /** Destructor.
     * Iterates through #m_thread_handle_map closing handles and closes #m_env.
     */
    ~BerkeleyDbFilesystem();

    /** Checkpoints the BerkeleyDB database.
     * This method calls <code>m_env.txn_checkpoint</code> to checkpoint the
     * database.  It passes in the value #m_checkpoint_size_kb.  Then if the
     * time of the last checkpoint has exceeded #m_log_gc_interval, it will call
     * <code>m_env.log_archive</code> to obtain a list of unused log files and
     * it will remove them.
     */
    void do_checkpoint();

    /** Check if we're the current master.
     * This method returns <i>true</i> if replication is disabled or if
     * we're the current elected master.
     * @return <i>true</i> if we're the master, <i>false</i> otherwise.
     */
    bool is_master() {
      // its the master if we're not doing replication or this is the replication master
      return (!m_replication_info.do_replication || m_replication_info.is_master);
    }

    /** Returns hostname of currently elected master.
     * @return Hostname of the currently elected master.
     */
    String get_current_master() {
      if (m_replication_info.is_master)
        return m_replication_info.localhost;
      else {
        auto it = m_replication_info.replica_map.find(m_replication_info.master_eid);
        if (it != m_replication_info.replica_map.end())
          return it->second;
        else
          return (String) "";
      }
    }

    /** Creates a new BerkeleyDB transaction.
     * @param txn Return parameter to hold newly created transaction
     */
    void start_transaction(BDbTxn &txn);

    bool get_xattr_i32(BDbTxn &txn, const String &fname,
                       const String &aname, uint32_t *valuep);
    void set_xattr_i32(BDbTxn &txn, const String &fname,
                       const String &aname, uint32_t value);
    bool get_xattr_i64(BDbTxn &txn, const String &fname,
                       const String &aname, uint64_t *valuep);
    void set_xattr_i64(BDbTxn &txn, const String &fname,
                       const String &aname, uint64_t value);
    void set_xattr(BDbTxn &txn, const String &fname,
                   const String &aname, const void *value, size_t value_len);
    bool get_xattr(BDbTxn &txn, const String &fname, const String &aname,
                   Hypertable::DynamicBuffer &vbuf);
    bool incr_attr(BDbTxn &txn, const String &fname, const String &aname,
                       uint64_t *valuep);

    bool exists_xattr(BDbTxn &txn, const String &fname, const String &aname);
    void del_xattr(BDbTxn &txn, const String &fname, const String &aname);
    void mkdir(BDbTxn &txn, const String &name);
    void unlink(BDbTxn &txn, const String &name);
    bool exists(BDbTxn &txn, String fname, bool *is_dir_p=0);
    void create(BDbTxn &txn, const String &fname, bool temp);
    void get_directory_listing(BDbTxn &txn, String fname,
                               std::vector<DirEntry> &listing);
    void get_directory_attr_listing(BDbTxn &txn, String fname, const String &aname,
                                    bool include_sub_entries,
                                    std::vector<DirEntryAttr> &listing);
    void get_directory_attr_listing(BDbTxn &txn, String fname, const String &aname,
                                    std::vector<DirEntryAttr> &listing);
    void get_all_names(BDbTxn &txn, std::vector<String> &names);
    bool list_xattr(BDbTxn &txn, const String& fname, std::vector<String> &anames);
    /*
     * Persists a new event in the StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param type Event type
     * @param id Event id
     * @param mask Event mask
     */
    void create_event(BDbTxn &txn, uint32_t type, uint64_t id, uint32_t mask);
    // for named event
    void create_event(BDbTxn &txn, uint32_t type, uint64_t id, uint32_t mask,
                      const String &name);
    void create_event(BDbTxn &txn, uint32_t type, uint64_t id, uint32_t mask, uint32_t mode);
    void create_event(BDbTxn &txn, uint32_t type, uint64_t id, uint32_t mask,
                      uint32_t mode, uint64_t generation);

    /*
     * Remove specified event from the StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Event id
     */
    void delete_event(BDbTxn &txn, uint64_t id);

    /*
     * Set the handles that are affected by this event
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Event id
     * @param handles array of handles that are affected by this event
     */
    void set_event_notification_handles(BDbTxn &txn, uint64_t id,
                                        const std::vector<uint64_t> &handles);

    /*
     * Check if the specified event is in the StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Event id
     * @return true if found false ow
     */
    bool event_exists(BDbTxn &txn, uint64_t id);

    /*
     * Persist a new SessionData object in the StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @param addr Stringified remote host address
     */
    void create_session(BDbTxn &txn, uint64_t id, const String &addr);

    /*
     * Delete info for specified session from StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     */
    void delete_session(BDbTxn &txn, uint64_t id);

    /*
     * Delete info for specified session from StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     */
    void expire_session(BDbTxn &txn, uint64_t id);


    /*
     * Store new handle opened by specified session
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @param handle_id Handle id
     */
    void add_session_handle(BDbTxn &txn, uint64_t id, uint64_t handle_id);

    /*
     * return all the handles a session has open
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @param handles vector into which open handle ids will be inserted
     */
    void get_session_handles(BDbTxn &txn, uint64_t id, std::vector<uint64_t> &handles);


    /*
     * Remove a handle from a session
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @param handle_id Handle id
     * @return true if handle was open and has been deleted
     */
    bool delete_session_handle(BDbTxn &txn, uint64_t id, uint64_t handle_id);


    /*
     * Check if specified session exists in StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @return true if session existsn StateDB, false ow
     */
    bool session_exists(BDbTxn &txn, uint64_t id);

    /*
     * Get name of session executable
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @return remote session executable
     */
    String get_session_name(BDbTxn &txn, uint64_t id);

    /*
     * Set name of session executable
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Session id
     * @param name of the session executable
     */
    void set_session_name(BDbTxn &txn, uint64_t id, const String &name);


    /*
     * Persist a new handle in StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param node_name Name of node associated with this handle
     * @param open_flags Open flags for this handle
     * @param event_mask Event notification mask
     * @param session_id Id of session associated with this handle
     * @param locked true if node is locked
     * @param del_state state of handle deletion operation
     */
    void create_handle(BDbTxn &txn, uint64_t id, String node_name,
        uint32_t open_flags, uint32_t event_mask, uint64_t session_id,
        bool locked, uint32_t del_state);

    /*
     * Delete all info for this handle from StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     */
    void delete_handle(BDbTxn &txn, uint64_t id);

    /*
     * Get open flags for handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @return open_flags for handle
     */
    uint32_t get_handle_open_flags(BDbTxn &txn, uint64_t id);

    /*
     * Set deletion state for handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param del_state new deletion state
     */
    void set_handle_del_state(BDbTxn &txn, uint64_t id, uint32_t del_state);

    /*
     * Get handle deletion state for handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @return deletion state for handle
     */
    uint32_t get_handle_del_state(BDbTxn &txn, uint64_t id);

    /*
     * Set open flags for handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param open_flags new flags
     */
    void set_handle_open_flags(BDbTxn &txn, uint64_t id, uint32_t open_flags);

    /*
     * Set event mask for handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param event_mask new event mask
     */
    void set_handle_event_mask(BDbTxn &txn, uint64_t id, uint32_t event_mask);

    /*
     * Get event mask for handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @return event mask
     */
    uint32_t get_handle_event_mask(BDbTxn &txn, uint64_t id);

    /*
     * Set the node associated with this handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param node_name name of node assoc with this handle
     */
    void set_handle_node(BDbTxn &txn, uint64_t id, const String &node_name);

    /*
     * Get the node associated with this handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param node_name name of node assoc with this handle, if not found node_name = ""
     */
    void get_handle_node(BDbTxn &txn, uint64_t id, String &node_name);

    /*
     * Get the session associated with this handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @return session id for this handle
     */
    uint64_t get_handle_session(BDbTxn &txn, uint64_t id);


    /*
     * Set the locked-ness this handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @param locked
     */
    void set_handle_locked(BDbTxn &txn, uint64_t id, bool locked);

    /*
     * Get the locked-ness this handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @return true if handle is locked
     */
    bool handle_is_locked(BDbTxn &txn, uint64_t id);

    /*
     * Check if info for this handle is in the StateDb
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param id Handle id
     * @return true if handle exists in StateDb
     */
    bool handle_exists(BDbTxn &txn, uint64_t id);

    /*
     * Persist a new node in StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param ephemeral true if node is ephemeral
     * @param lock_generation node lock generation
     * @param cur_lock_mode node lock mode
     * @param exclusive_handle handle id of exclusive lock handle
     */
    void create_node(BDbTxn &txn, const String &name, bool ephemeral=false,
        uint64_t lock_generation=0, uint32_t cur_lock_mode=0, uint64_t exclusive_handle=0);

    /*
     * Set the lock generation this node
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param lock_generation
     */
    void set_node_lock_generation(BDbTxn &txn, const String &name, uint64_t lock_generation);

    /*
     * Increment the lock generation this node
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return current lock generation (after increment)
     */
    uint64_t incr_node_lock_generation(BDbTxn &txn, const String &name);


    /*
     * Set the node ephemeral-ness
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param ephemeral
     */
    void set_node_ephemeral(BDbTxn &txn, const String &name, bool ephemeral);

    /*
     * Set the node ephemeral-ness
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return true if node is ephemeral
     */
    bool node_is_ephemeral(BDbTxn &txn, const String &name);

    /*
     * Set the node current lock mode
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param lock_mode
     */
    void set_node_cur_lock_mode(BDbTxn &txn, const String &name, uint32_t lock_mode);

    /*
     * Get the node current lock mode
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return lock_mode
     */
    uint32_t get_node_cur_lock_mode(BDbTxn &txn, const String &name);

    /*
     * Set the node exclusive_lock_handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param exclusive_lock_handle
     */
    void set_node_exclusive_lock_handle(BDbTxn &txn, const String &name,
                                        uint64_t exclusive_lock_handle);

    /*
     * Get the node exclusive_lock_handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return exclusive lock handle
     */
    uint64_t get_node_exclusive_lock_handle(BDbTxn &txn, const String &name);


    /*
     * Add a handle to the node
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param handle
     */
    void add_node_handle(BDbTxn &txn, const String &name, uint64_t handle);

    /*
     * Remove a handle from the node
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param handle
     */
    void delete_node_handle(BDbTxn &txn, const String &name, uint64_t handle);

    /*
     * Returns whether any handles have this node open
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return true if at least one handle has this node open
     */
    bool node_has_open_handles(BDbTxn &txn, const String &name);

    /*
     * Check if a node has any pending lock requests from non-expired handles
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return true if node has at least one pending lock request
     */
    bool node_has_pending_lock_request(BDbTxn &txn, const String &name);

    /** Check if a node has any pending lock requests from non-expired handles.
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param front_req will contain the first pending request if this method returns true
     * @return true if node has at least one pending lock request
     */
    bool get_node_pending_lock_request(BDbTxn &txn, const String &name,
                                       LockRequest &front_req);


    /** Adds a lock request.
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param request Lock request
     */
    void add_node_pending_lock_request(BDbTxn &txn, const String &name,
                                       LockRequest &request);
    /*
     * Remove a lock request to the node
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param handle handle requesting lock
     */
    void delete_node_pending_lock_request(BDbTxn &txn, const String &name, uint64_t handle);

    /*
     * Add a shared lock handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param handle handle requesting lock
     */
    void add_node_shared_lock_handle(BDbTxn &txn, const String &name, uint64_t handle);

    /*
     * Get a map of(handle id,  session id) for notifications registered for a certain
     * event occuring to a node
     *
     * @param txn BerkeleyDB txn
     * @param name Node name
     * @param event_mask indicates the event that is ocurring
     * @param handles_to_sessions map specifying notifications to be sent
     * @return true if there are some notifications that need to be sent out
     */
    bool get_node_event_notification_map(BDbTxn &txn, const String &name, uint32_t event_mask,
        NotificationMap &handles_to_sessions);

    /*
     * Get all open handles for a node
     *
     * @param txn BerkeleyDB txn
     * @param name Node name
     * @param handles vector of node handles
     */
    void get_node_handles(BDbTxn &txn, const String &name, std::vector<uint64_t> &handles);

    /*
     * Remove node shared lock handle
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @param handle_id to be deleted
     */
    void delete_node_shared_lock_handle(BDbTxn &txn, const String &name, uint64_t handle_id);

    /*
     * Delete all info for this node from StateDB
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return false if node doesn't exist true if node was deleted
     */
    bool delete_node(BDbTxn &txn, const String &name);

    /*
     * Check if info for this node is in the StateDb
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return true if node exists in StateDb
     */
    bool node_exists(BDbTxn &txn, const String &name);

    /*
     * Check if node has any shared lock handles
     *
     * @param txn BerkeleyDB txn for this DB update
     * @param name Node name
     * @return true if node has shared lock handles
     */
    bool node_has_shared_lock_handles(BDbTxn &txn, const String &name);

    uint64_t get_next_id_i64(BDbTxn &txn, IdentifierType id_type, bool increment = false);

    static const char NODE_ATTR_DELIM = 0x01;


  private:
    /*
     * Initialize per worker thread DB handles
     */
    void init_db_handles(const std::vector<Thread::id> &thread_ids);
    BDbHandlesPtr get_db_handles();
    void build_attr_key(BDbTxn &, String &keystr,
                        const String &aname, Dbt &key);
    static void db_event_callback(DbEnv *dbenv, uint32_t which, void *info);
    static void db_err_callback(const DbEnv *dbenv, const char *errpfx, const char *msg);
    static void db_msg_callback(const DbEnv *dbenv, const char *msgpfx, const char *msg);
    static void db_msg_callback(const DbEnv *dbenv, const char *msg);

    ReplicationInfo m_replication_info;
    String m_base_dir;
    DbEnv  m_env;
    uint32_t m_db_flags {};
    static const char *ms_name_namespace_db;
    static const char *ms_name_state_db;
    typedef std::map<Thread::id, BDbHandlesPtr> ThreadHandleMap;
    ThreadHandleMap m_thread_handle_map;

    /// Checkpoint size threshold in kilobytes
    uint32_t m_checkpoint_size_kb;
    uint32_t m_max_unused_logs;
    std::chrono::steady_clock::duration m_log_gc_interval;
    std::chrono::steady_clock::time_point m_last_log_gc_time;
  };

  /** @} */

} // namespace Hyperspace

#endif // Hyperspace_BerkeleyDbFilesystem_h
