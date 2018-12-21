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

/// @file
/// Definitions for SshSocketHandler.
/// This file contains type definitions for SshSocketHandler, a raw socket
/// handler for implementing an ssh protocol driver.

#include <Common/Compat.h>

#include "SshSocketHandler.h"

#include <Common/FileUtils.h>
#include <Common/Logger.h>

#include <AsyncComm/PollEvent.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <set>
#include <thread>

#include <fcntl.h>
#include <strings.h>
#include <sys/ioctl.h>

using namespace Hypertable;
using namespace std;

#define SSH_READ_PAGE_SIZE 8192

#define LOG_PREFIX "[" << m_hostname << "] === SshSocketHandler.cc:" << __LINE__ << " === "

namespace {

  enum {
    STATE_INITIAL = 0,
    STATE_CREATE_SESSION = 1,
    STATE_COMPLETE_CONNECTION = 2,
    STATE_VERIFY_KNOWNHOST = 3,
    STATE_AUTHENTICATE = 4,
    STATE_CONNECTED = 5,
    STATE_COMPLETE_CHANNEL_SESSION_OPEN = 6,
    STATE_CHANNEL_REQUEST_EXEC = 7,
    STATE_CHANNEL_REQUEST_READ = 8
  };

  const char *state_str(int state) {
    switch (state) {
    case (STATE_INITIAL):
      return "INITIAL";
    case (STATE_COMPLETE_CONNECTION):
      return "COMPLETE_CONNECTION";
    case (STATE_CREATE_SESSION):
      return "CREATE_SESSION";
    case (STATE_VERIFY_KNOWNHOST):
      return "VERIFY_KNOWNHOST";
    case (STATE_AUTHENTICATE):
      return "AUTHENTICATE";
    case (STATE_CONNECTED):
      return "CONNECTED";
    case (STATE_COMPLETE_CHANNEL_SESSION_OPEN):
      return "STATE_COMPLETE_CHANNEL_SESSION_OPEN";
    case (STATE_CHANNEL_REQUEST_EXEC):
      return "STATE_CHANNEL_REQUEST_EXEC";
    case (STATE_CHANNEL_REQUEST_READ):
      return "STATE_CHANNEL_REQUEST_READ";
    default:
      break;
    }
    return "UNKNOWN";
  }

  void log_callback_function(ssh_session session, int priority,
                             const char *message, void *userdata) {
    ((SshSocketHandler *)userdata)->log_callback(session, priority, message);
  }

  int auth_callback_function(const char *prompt, char *buf, size_t len,
                             int echo, int verify, void *userdata) {
    return ((SshSocketHandler *)userdata)->auth_callback(prompt, buf, len, echo, verify);
  }

  void connect_status_callback_function(void *userdata, float status) {
    ((SshSocketHandler *)userdata)->connect_status_callback(status);
  }

  void global_request_callback_function(ssh_session session,
                                        ssh_message message, void *userdata) {
    ((SshSocketHandler *)userdata)->global_request_callback(session, message);
  }

  void exit_status_callback_function(ssh_session session,
                                     ssh_channel channel,
                                     int exit_status,
                                     void *userdata) {
    ((SshSocketHandler *)userdata)->set_exit_status(exit_status);
  }

}

bool SshSocketHandler::ms_debug_enabled {};
int  SshSocketHandler::ms_libssh_verbosity {SSH_LOG_PROTOCOL};

void SshSocketHandler::enable_debug() { ms_debug_enabled = true; }

void SshSocketHandler::set_libssh_verbosity(const std::string &value) {
  if (!strcasecmp(value.c_str(), "none"))
    ms_libssh_verbosity = SSH_LOG_NOLOG;
  else if (!strcasecmp(value.c_str(), "warning"))
    ms_libssh_verbosity = SSH_LOG_WARNING;
  else if (!strcasecmp(value.c_str(), "protocol"))
    ms_libssh_verbosity = SSH_LOG_PROTOCOL;
  else if (!strcasecmp(value.c_str(), "packet"))
    ms_libssh_verbosity = SSH_LOG_PACKET;
  else if (!strcasecmp(value.c_str(), "functions"))
    ms_libssh_verbosity = SSH_LOG_FUNCTIONS;
  else {
    cout << "Unrecognized libssh logging level: " << value << endl;
    quick_exit(EXIT_FAILURE);
  }
}

SshSocketHandler::SshSocketHandler(const string &hostname)
  : m_hostname(hostname), m_log_collector(1024),
    m_stdout_collector(SSH_READ_PAGE_SIZE), m_stderr_collector(1024) {

  m_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_sd < 0) {
    m_error = string("socket(AF_INET, SOCK_STREAM, 0) fialed - ") + strerror(errno);
    return;
  }


  struct hostent *server = gethostbyname(m_hostname.c_str());
  if (server == nullptr) {
    m_error = string("gethostbyname('") + m_hostname + "') failed - " + hstrerror(h_errno);
    deregister(m_sd);
    return;
  }

  struct sockaddr_in serv_addr;
  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, 
        (char *)&serv_addr.sin_addr.s_addr,
        server->h_length);
  serv_addr.sin_port = htons(22);

  m_comm_address = CommAddress(serv_addr);

  m_poll_interest = PollEvent::WRITE;

  m_comm = Comm::instance();

  m_state = STATE_CREATE_SESSION;

  // Set to non-blocking
  FileUtils::set_flags(m_sd, O_NONBLOCK);

  int tries = 5;
  int retry = 0;
  while (connect(m_sd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
    if (errno == EINTR) {
      this_thread::sleep_for(chrono::milliseconds(1000));
      continue;
    }
	else if (errno == ETIMEDOUT || errno == ECONNABORTED || errno == ECONNREFUSED || errno == ECONNRESET) {
		if (retry < tries) {
			this_thread::sleep_for(chrono::milliseconds(30));
			retry++;
			continue;
		}
		m_error = string("connect(") + InetAddr::format(serv_addr) + ") failed - " + strerror(errno);
		deregister(m_sd);
		return;
	}   
    else if (errno != EINPROGRESS && errno != EALREADY && errno != EISCONN) {
      m_error = string("connect(") + InetAddr::format(serv_addr) + ") failed - " + strerror(errno);
      deregister(m_sd);
      return;
    }
    m_state = STATE_INITIAL;
    break;
  }
  int rc = m_comm->register_socket(m_sd, m_comm_address, this);
  if (rc != Error::OK) {
    m_error = string("Comm::register_socket(") + InetAddr::format(serv_addr) + ") failed - " + strerror(errno);
    deregister(m_sd);
  }
}


SshSocketHandler::~SshSocketHandler() {
  if (m_ssh_session) {//m_state != STATE_INITIAL && 
    ssh_disconnect(m_ssh_session);
    ssh_free(m_ssh_session);
  }
}

bool SshSocketHandler::handle(int sd, int events) {
  lock_guard<mutex> lock(m_mutex);
  int rc;

  if (ms_debug_enabled)
    cerr << LOG_PREFIX << "Entering handler (events="
         << PollEvent::to_string(events) << ", state=" << state_str(m_state)
         << ")\n";

  while (true) {
    switch (m_state) {

    case (STATE_INITIAL):
      {
        int sockerr = 0;
        socklen_t sockerr_len = sizeof(sockerr);
        if (getsockopt(m_sd, SOL_SOCKET, SO_ERROR, &sockerr, &sockerr_len) < 0) {
          m_error = string("getsockopt(SO_ERROR) failed (") + strerror(errno) + ")";
          m_cond.notify_all();
          return false;
        }
        if (sockerr) {
          m_error = string("connect() completion error (") + strerror(errno) + ")";
          m_cond.notify_all();
          return false;
        }
      }
      m_poll_interest = PollEvent::READ;
      m_state = STATE_CREATE_SESSION;

    case (STATE_CREATE_SESSION):
      {    
        m_ssh_session = ssh_new();

        HT_ASSERT(m_ssh_session);

        char *home = getenv("HOME");
        if (home == nullptr)
          HT_FATAL("Environment variable HOME is not set");
        string ssh_dir(home);
        ssh_dir.append("/.ssh");
        ssh_options_set(m_ssh_session, SSH_OPTIONS_SSH_DIR, ssh_dir.c_str());

        int verbosity = ms_libssh_verbosity;
        ssh_options_set(m_ssh_session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

        ssh_options_set(m_ssh_session, SSH_OPTIONS_HOST, m_hostname.c_str());

        ssh_options_set(m_ssh_session, SSH_OPTIONS_FD, &m_sd);

        ssh_set_blocking(m_ssh_session, 0);

        // set callbacks
        memset(&m_callbacks, 0, sizeof(m_callbacks));
        m_callbacks.userdata = this;
        m_callbacks.auth_function = auth_callback_function;
        m_callbacks.log_function = log_callback_function;
        m_callbacks.connect_status_function = connect_status_callback_function;
        m_callbacks.global_request_function = global_request_callback_function;
        ssh_callbacks_init(&m_callbacks);

        rc = ssh_set_callbacks(m_ssh_session, &m_callbacks);
        if (rc == SSH_ERROR) {
          m_error = string("ssh_set_callbacks() failed - ") + ssh_get_error(m_ssh_session);
          m_cond.notify_all();
          return false;
        }

        // Load in preferred order a SSH Client Configuration file
        if (FileUtils::exists("../conf/ht_ssh_config"))
          ssh_options_parse_config(m_ssh_session, "../conf/ht_ssh_config");
        else if (FileUtils::exists("/etc/ssh/ssh_config"))
          ssh_options_parse_config(m_ssh_session, "/etc/ssh/ssh_config");
        else {
          string config_file = ssh_dir + "/config";
          if (FileUtils::exists(config_file))
            ssh_options_parse_config(m_ssh_session, config_file.c_str());
        }
        rc = ssh_connect(m_ssh_session);
        if (rc == SSH_OK) {
          m_state = STATE_VERIFY_KNOWNHOST;
          continue;
        }
        if (rc == SSH_ERROR) {
          m_error = string("ssh_connect() failed - ") + ssh_get_error(m_ssh_session);
          m_cond.notify_all();
          return false;
        }
        else if (rc == SSH_AGAIN) {
          m_state = STATE_COMPLETE_CONNECTION;
          break;
        }
      }

    case (STATE_COMPLETE_CONNECTION):
      rc = ssh_connect(m_ssh_session);
      if (rc == SSH_AGAIN)
        break;
      else if (rc == SSH_ERROR) {
        m_error = string("ssh_connect() failed - ") + ssh_get_error(m_ssh_session);
        m_cond.notify_all();
        return false;
      }
      HT_ASSERT(rc == SSH_OK);
      m_state = STATE_VERIFY_KNOWNHOST;

    case (STATE_VERIFY_KNOWNHOST):
      if (!verify_knownhost()) {
        m_cond.notify_all();
        return false;
      }
      m_state = STATE_AUTHENTICATE;

    case (STATE_AUTHENTICATE):
      rc = ssh_userauth_publickey_auto(m_ssh_session, nullptr, nullptr);
      if (rc == SSH_AUTH_ERROR) {
        m_error = string("authentication failure - ") + ssh_get_error(m_ssh_session);
        m_cond.notify_all();
        return false;
      }
      else if (rc == SSH_AUTH_AGAIN) {
        m_poll_interest = PollEvent::READ;
        if (socket_has_data())
          continue;
        break;
      }
      else if (rc == SSH_AUTH_DENIED) {
        m_error = string("publickey authentication denied");
        m_cond.notify_all();
        return false;
      }

      HT_ASSERT(rc == SSH_AUTH_SUCCESS);
      m_state = STATE_CONNECTED;
      m_poll_interest = PollEvent::READ;
      m_cond.notify_all();
      break;

    case (STATE_COMPLETE_CHANNEL_SESSION_OPEN):
      rc = ssh_channel_open_session(m_channel);
      if (rc == SSH_AGAIN) {
        if (socket_has_data())
          continue;
        break;
      }
      else if (rc == SSH_ERROR) {
        ssh_channel_free(m_channel);
        m_channel = 0;
        m_error = string("ssh_channel_open_session() failed - ") + ssh_get_error(m_ssh_session);
        m_cond.notify_all();
        return false;
      }
      HT_ASSERT(rc == SSH_OK);
      m_state = STATE_CHANNEL_REQUEST_EXEC;

    case (STATE_CHANNEL_REQUEST_EXEC):
      rc = ssh_channel_request_exec(m_channel, m_command.c_str());
      m_poll_interest = PollEvent::READ;
      if (rc == SSH_AGAIN) {
        if (socket_has_data())
          continue;
        break;
      }
      else if (rc == SSH_ERROR) {
        m_error = string("ssh_request_exec() failed - ") + ssh_get_error(m_ssh_session);
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_channel = 0;
        m_cond.notify_all();
        return false;
      }
      HT_ASSERT(rc == SSH_OK);
      m_state = STATE_CHANNEL_REQUEST_READ;
      
    case (STATE_CHANNEL_REQUEST_READ):
      /*
      switch(ssh_get_status(m_ssh_session)){
        case SSH_READ_PENDING:
          std::cout << "SSH_READ_PENDING";
          break;
        case SSH_WRITE_PENDING:
          std::cout << "SSH_WRITE_PENDING";
	        m_state = STATE_CONNECTED;
          return true;
        case SSH_CLOSED:
          std::cout << "SSH_CLOSED";
          return false;
        case SSH_CLOSED_ERROR:
          std::cout << "SSH_CLOSED_ERROR";
          return false;
      }
      */
		for (int is_stderr = 0; is_stderr <= 1;) {
			while (true) {

				if (m_stdout_buffer.base == 0)
					m_stdout_buffer = m_stdout_collector.allocate_buffer();

				int nbytes = ssh_channel_read_nonblocking(m_channel,
					m_stdout_buffer.ptr,
					m_stdout_buffer.remain(),
					is_stderr);

				if (nbytes == SSH_ERROR) {
					m_error = string("ssh_channel_read_nonblocking() failed - ") + ssh_get_error(m_ssh_session);
					ssh_channel_close(m_channel);
					ssh_channel_free(m_channel);
					m_channel = 0;
					m_cond.notify_all();
					return false;
				}
				else if (nbytes == SSH_EOF) {
					m_channel_is_eof = true;
					break;
				}
				else if (nbytes <= 0)
					break;

				if (nbytes > 0 && *m_stdout_buffer.ptr == 0)
					continue;

				if (m_terminal_output)
					write_to_stdout((const char *)m_stdout_buffer.ptr, nbytes);

				m_stdout_buffer.ptr += (size_t)nbytes;
				if (m_stdout_buffer.fill() == SSH_READ_PAGE_SIZE) {
					m_stdout_collector.add(m_stdout_buffer);
					m_stdout_buffer = SshOutputCollector::Buffer();
				}
			}
			is_stderr++;
		}


      if (m_channel_is_eof || ssh_channel_is_eof(m_channel)) {
        int exit_status = ssh_channel_get_exit_status(m_channel);
	// If ssh_channel_get_exit_status() returns -1 and the exit status has not yet
	/// been set, then we need to read again to get the exit status
        if (ms_debug_enabled)
          cerr << LOG_PREFIX << "At EOF (exit_status=" << exit_status
               << ", status_is_set="
               << (m_command_exit_status_is_set ? "true" : "false") << ")\n";
	if (exit_status == -1 && !m_command_exit_status_is_set)
          break;
	if (!m_command_exit_status_is_set) {
	  m_command_exit_status_is_set = true;
	  m_command_exit_status = exit_status;
	}
	m_stdout_collector.add(m_stdout_buffer);
        m_stdout_buffer = SshOutputCollector::Buffer();
	m_stderr_collector.add(m_stderr_buffer);
        m_stderr_buffer = SshOutputCollector::Buffer();
	ssh_channel_close(m_channel);
	ssh_channel_free(m_channel);
	m_channel = 0;
	m_state = STATE_CONNECTED;
	m_poll_interest = 0;
	m_cond.notify_all();
      }
      break;

    case (STATE_CONNECTED):
      break;

    default:
      HT_FATALF("Unrecognize state - %d", m_state);
    }
    break;
  }

  if (ms_debug_enabled)
    cerr << LOG_PREFIX << "Leaving handler (poll_interest="
         << PollEvent::to_string(m_poll_interest)
         << ", state=" << state_str(m_state) << ")\n";
  
  return true;
}

void SshSocketHandler::deregister(int sd) {
  ::close(m_sd);
  m_sd = -1;
  m_poll_interest = PollEvent::WRITE;
}

void SshSocketHandler::log_callback(ssh_session session, int priority, const char *message) {
  size_t len;
  if (ms_debug_enabled) {
    cerr << "[" << m_hostname << "] " << message << "\n";
    return;
  }
  if (ms_libssh_verbosity <= SSH_LOG_PROTOCOL && priority <= 1)
    return;
  if (m_log_buffer.base == 0)
    m_log_buffer = m_log_collector.allocate_buffer();
  while (m_log_buffer.remain() < strlen(message)) {
    len = m_log_buffer.remain();
    m_log_buffer.add(message, len);
    message += len;
    m_log_collector.add(m_log_buffer);
    m_log_buffer = m_log_collector.allocate_buffer();    
  }
  if (*message) {
    len = strlen(message);
    m_log_buffer.add(message, len);
  }
  // Add newline
  if (m_log_buffer.remain() == 0) {
    m_log_collector.add(m_log_buffer);
    m_log_buffer = m_log_collector.allocate_buffer();    
  }
  m_log_buffer.add("\n", 1);
}

int SshSocketHandler::auth_callback(const char *prompt, char *buf, size_t len, int echo, int verify) {
  if (ms_debug_enabled)
    cerr << LOG_PREFIX << "auth_callback (" << prompt << ", buflen=" << len
         << ", echo=" << echo << ", verify=" << verify << ")\n";
  return -1;
}

void SshSocketHandler::connect_status_callback(float status) {
  if (ms_debug_enabled)
    cerr << LOG_PREFIX << "connect_status_callback " << (int)(status*100.0) << "%\n";
}

void SshSocketHandler::global_request_callback(ssh_session session, ssh_message message) {
  if (ms_debug_enabled)
    cerr << LOG_PREFIX << "global_request_callback (type=" << ssh_message_type(message)
         << ", subtype=" << ssh_message_subtype(message) << ")\n";
}

void SshSocketHandler::set_exit_status(int exit_status) {
  m_command_exit_status = exit_status;
  m_command_exit_status_is_set = true;
  m_channel_is_eof = true;
}

bool SshSocketHandler::wait_for_connection(chrono::system_clock::time_point deadline) {
  unique_lock<mutex> lock(m_mutex);
  while (m_state != STATE_CONNECTED && m_error.empty() && !m_cancelled) {
    if (m_cond.wait_until(lock, deadline) == cv_status::timeout) {
      m_error = "timeout";
      return false;
    }
  }
  return m_error.empty();
}

bool SshSocketHandler::issue_command(const std::string &command) {
  lock_guard<mutex> lock(m_mutex);

  m_command = command;
  m_command_exit_status = 0;
  m_command_exit_status_is_set = false;

  m_channel = ssh_channel_new(m_ssh_session);
  m_channel_is_eof = false;
  HT_ASSERT(m_channel);

  ssh_channel_set_blocking(m_channel, 0);

  // set callbacks
  memset(&m_channel_callbacks, 0, sizeof(m_channel_callbacks));
  m_channel_callbacks.userdata = this;
  m_channel_callbacks.channel_exit_status_function = exit_status_callback_function;
  ssh_callbacks_init(&m_channel_callbacks);
  int rc = ssh_set_channel_callbacks(m_channel, &m_channel_callbacks);
  if (rc == SSH_ERROR) {
    m_error = string("ssh_set_channel_callbacks() failed - ") + ssh_get_error(m_ssh_session);
    return false;
  }

  while (true) {
    rc = ssh_channel_open_session(m_channel);
    if (rc == SSH_AGAIN) {
      m_state = STATE_COMPLETE_CHANNEL_SESSION_OPEN;
      m_poll_interest = PollEvent::READ;
      if (socket_has_data())
        continue;
      return true;
    }
    else if (rc == SSH_ERROR) {
      ssh_channel_free(m_channel);
      m_channel = 0;
      m_error = string("ssh_channel_open_session() failed - ") + ssh_get_error(m_ssh_session);
      m_state = STATE_CONNECTED;
      return false;
    }
    break;
  }

  HT_ASSERT(rc == SSH_OK);
  m_state = STATE_CHANNEL_REQUEST_EXEC;
  m_poll_interest = PollEvent::WRITE;
  return true;
}

bool SshSocketHandler::wait_for_command_completion() {
  unique_lock<mutex> lock(m_mutex);
  while (m_state != STATE_CONNECTED && m_error.empty() &&
         !m_command_exit_status_is_set && !m_cancelled)
    m_cond.wait(lock);
  return m_error.empty() && m_command_exit_status == 0 &&
    (!m_cancelled || m_state == STATE_CONNECTED);
}

void SshSocketHandler::cancel() {
  unique_lock<mutex> lock(m_mutex);
  m_cancelled = true;
  if (m_channel) {
    ssh_channel_close(m_channel);
    ssh_channel_free(m_channel);
    m_channel = 0;
  }
  if (m_state != STATE_INITIAL && m_ssh_session) {
    ssh_disconnect(m_ssh_session);
    ssh_free(m_ssh_session);
  }
  m_cond.notify_all();
}


void SshSocketHandler::set_terminal_output(bool val) {
  unique_lock<mutex> lock(m_mutex);

  m_terminal_output = val;
  if (!m_terminal_output)
    return;

  // Send stdout collected so far to output stream
  bool first = true;
  if (m_stdout_buffer.fill()) {
    m_stdout_collector.add(m_stdout_buffer);
    m_stdout_buffer = SshOutputCollector::Buffer();
  }
  m_line_prefix_needed_stdout = true;
  if (!m_stdout_collector.empty()) {
    for (auto & line : m_stdout_collector) {
      if (first)
        first = false;
      else
        cout << "\n";
      cout << "[" << m_hostname << "] " << line;
    }
    if (!m_stdout_collector.last_line_is_partial())
      cout << "\n";
    else
      m_line_prefix_needed_stdout = false;
    cout << flush;
  }

  // Send stderr collected so far to output stream
  first = true;
  if (m_stderr_buffer.fill()) {
    m_stderr_collector.add(m_stderr_buffer);
    m_stderr_buffer = SshOutputCollector::Buffer();
  }
  m_line_prefix_needed_stderr = true;
  if (!m_stderr_collector.empty()) {
    for (auto & line : m_stderr_collector) {
      if (first)
        first = false;
      else
        cerr << "\n";
      cerr << "[" << m_hostname << "] " << line;
    }
    if (!m_stderr_collector.last_line_is_partial())
      cerr << "\n";
    else
      m_line_prefix_needed_stderr = false;
    cerr << flush;
  }
}

void SshSocketHandler::dump_log(std::ostream &out) {
  if (m_log_buffer.fill()) {
    m_log_collector.add(m_log_buffer);
    m_log_buffer = SshOutputCollector::Buffer();
  }
  if (!m_error.empty()) {
    for (auto & line : m_log_collector)
      out << "[" << m_hostname << "] " << line << "\n";
    out << "[" << m_hostname << "] ERROR " << m_error << endl;
  }
  /**
  if (m_command_exit_status != 0)
    out << "[" << m_hostname << "] exit status = " << m_command_exit_status << "\n";
  */
  out << flush;  
}


bool SshSocketHandler::verify_knownhost() {
  unsigned char *hash {};
  size_t hlen {};
  int rc;

  int state = ssh_session_is_known_server(m_ssh_session);

  ssh_key key;
  rc = ssh_get_server_publickey(m_ssh_session, &key);
  if (rc != SSH_OK) {
    m_error = string("unable to obtain public key - ") + ssh_get_error(m_ssh_session);
    return false;
  }

  rc = ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA1, &hash, &hlen);
  if (rc != SSH_OK) {
    ssh_key_free(key);
    m_error = "problem computing public key hash";
    return false;
  }

  ssh_key_free(key);

  switch (state) {

  case SSH_KNOWN_HOSTS_OK:
    break;

  case SSH_KNOWN_HOSTS_CHANGED:
    m_error = "host key has changed";
    ssh_clean_pubkey_hash(&hash);
    return false;

  case SSH_KNOWN_HOSTS_OTHER:
    m_error = "Key mis-match with one in known_hosts";
    ssh_clean_pubkey_hash(&hash);
    return false;

  case SSH_KNOWN_HOSTS_NOT_FOUND:
  case SSH_KNOWN_HOSTS_UNKNOWN:

    if (ssh_session_update_known_hosts(m_ssh_session) != SSH_OK) {
      m_error = "problem writing known hosts file";
      ssh_clean_pubkey_hash(&hash);
      return false;
    }
    break;

  case SSH_KNOWN_HOSTS_ERROR:
    m_error = ssh_get_error(m_ssh_session);
    return false;
  }
  ssh_clean_pubkey_hash(&hash);
  return true;
}


void SshSocketHandler::write_to_stdout(const char *output, size_t len) {
  const char *base = output;
  const char *end = output + len;
  const char *ptr;

  while (base < end) {
    if (m_line_prefix_needed_stdout)
      cout << "[" << m_hostname << "] ";

    for (ptr = base; ptr<end; ptr++) {
      if (*ptr == '\n')
        break;
    }

    if (ptr < end) {
      cout << string(base, ptr-base) << endl;
      base = ptr+1;
      m_line_prefix_needed_stdout = true;
    }
    else {
      cout << string(base, ptr-base);
      m_line_prefix_needed_stdout = false;
      break;
    }
  }
  cout << flush;
}


void SshSocketHandler::write_to_stderr(const char *output, size_t len) {
  const char *base = output;
  const char *end = output + len;
  const char *ptr;

  while (base < end) {
    if (m_line_prefix_needed_stderr)
      cerr << "[" << m_hostname << "] ";

    for (ptr = base; ptr<end; ptr++) {
      if (*ptr == '\n')
        break;
    }

    if (ptr < end) {
      cerr << string(base, ptr-base) << endl;
      base = ptr+1;
      m_line_prefix_needed_stderr = true;
    }
    else {
      cerr << string(base, ptr-base);
      m_line_prefix_needed_stderr = false;
      break;
    }
  }
  cerr << flush;
}


bool SshSocketHandler::socket_has_data() {
  int count;
  ioctl(m_sd, FIONREAD, &count);
  return count != 0;
}
