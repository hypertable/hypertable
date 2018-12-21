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

#ifndef Hyperspace_SessionData_h
#define Hyperspace_SessionData_h

#include "Notification.h"

#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <set>

namespace Hyperspace {

  using namespace Hypertable;

  class SessionData {
  public:
    SessionData(const sockaddr_in &_addr, gInt32tPtr lease_interval, uint64_t _id)
      : addr(_addr), m_lease_interval(lease_interval), id(_id) {
      expire_time = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(lease_interval->get());
    }

    void add_notification(Notification *notification) {
      std::lock_guard<std::mutex> lock(mutex);

      if (expired) {
        notification->event_ptr->decrement_notification_count();
        delete notification;
      }
      else
        notifications.push_back(notification);
    }

    void purge_notifications(std::set<uint64_t> &delivered_events) {
      std::lock_guard<std::mutex> lock(mutex);
      std::list<Notification *>::iterator iter = notifications.begin();
      while (iter != notifications.end()) {
        if (delivered_events.count((*iter)->event_ptr->get_id()) > 0) {
          (*iter)->event_ptr->decrement_notification_count();
          delete *iter;
          iter = notifications.erase(iter);
        }
        else
          ++iter;
      }
    }

    const char* get_name() const
    {
      return name.c_str();
    }

    CommBuf * serialize_notifications_for_keepalive(CommHeader &header, uint32_t &len)
    {
      std::lock_guard<std::mutex> lock(mutex);
      std::list<Notification *>::iterator iter;
      CommBuf *cbuf =0;
      for (iter = notifications.begin(); iter != notifications.end(); ++iter) {
        len += 8;  // handle
        len += (*iter)->event_ptr->encoded_length();
      }

      cbuf = new CommBuf(header, len);
      cbuf->append_i64(id);
      cbuf->append_i32(Error::OK);
      cbuf->append_i32(notifications.size());
      for (iter = notifications.begin(); iter != notifications.end(); ++iter) {
        cbuf->append_i64((*iter)->handle);
        (*iter)->event_ptr->encode(cbuf);
      }
      return cbuf;
    }

    bool renew_lease() {
      std::lock_guard<std::mutex> lock(mutex);
      auto now = std::chrono::steady_clock::now();
      if (expire_time < now)
        return false;
      expire_time = now + std::chrono::milliseconds(m_lease_interval->get());
      return true;
    }

    uint64_t get_id() const { return id; }

    const struct sockaddr_in& get_addr() const { return addr; }

    void extend_lease(std::chrono::milliseconds millis) {
      std::lock_guard<std::mutex> lock(mutex);
      expire_time += millis;
    }

    bool is_expired(std::chrono::steady_clock::time_point now) {
      std::lock_guard<std::mutex> lock(mutex);
      return expired || expire_time < now;
    }

    void expire() {
      std::lock_guard<std::mutex> lock(mutex);
      if (expired)
        return;
      expired = true;
      std::list<Notification *>::iterator iter = notifications.begin();
      while (iter != notifications.end()) {
        (*iter)->event_ptr->decrement_notification_count();
        delete *iter;
        iter = notifications.erase(iter);
      }
    }

    void set_expire_time_now() {
      std::lock_guard<std::mutex> lock(mutex);
      expire_time = std::chrono::steady_clock::now();
    }

    void set_name(const String &name_) {
      std::lock_guard<std::mutex> lock(mutex);
      name = name_;
    }

    friend struct LtSessionData;

  private:

    std::mutex mutex;
    struct sockaddr_in addr;
    std::chrono::steady_clock::time_point expire_time;
    gInt32tPtr m_lease_interval {};
    uint64_t id;
    bool expired {};
    std::list<Notification *> notifications;
    String name;
  };

  typedef std::shared_ptr<SessionData> SessionDataPtr;

  struct LtSessionData {
    bool operator()(const SessionDataPtr &x, const SessionDataPtr &y) const {
      return std::chrono::operator>(x->expire_time, y->expire_time);
    }
  };

}

#endif // Hyperspace_SessionData_h
