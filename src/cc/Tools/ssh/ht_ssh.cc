/*
 * Copyright (C) 2007-2014 Hypertable, Inc.
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

/// @file
/// main function definition for ht_ssh.
/// This file contains the main function definition for ht_ssh, a tool for
/// establishing ssh connections and executing commands on hosts in parallel.

#include <Common/Compat.h>

#include "SshSocketHandler.h"
#include "SshThreadsCallbacks.h"
#include "SshOutputCollector.h"

#include <Common/Init.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ReactorFactory.h>

#include <Common/Error.h>
#include <Common/HostSpecification.h>
#include <Common/Logger.h>
#include <Common/Random.h>
#include <Common/System.h>
#include <Common/Timer.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

using namespace Hypertable;
using namespace std;

namespace {
  
  std::mutex g_mutex;
  bool g_handlers_created {};
  bool g_cancelled {};
  vector<SshSocketHandlerPtr> g_handlers;

  void sig(int i) {
    lock_guard<mutex> lock(g_mutex);
    g_cancelled = true;
    if (g_handlers_created) {
      for (auto & handler : g_handlers)
        handler->cancel();
    }
  }

  void dump_usage_and_exit() {
    cout << "\n";
    cout << "ht_ssh version " << version_string() << "\n";
    cout << "\n";
    cout << "This application uses libssh 0.8.5 (https://www.libssh.org/)\n";
    cout << "libssh is licensed under the GNU Lesser General Public License\n";
    cout << "\n";
    cout << "usage: ht_ssh [options] <hosts-specification> <command>\n";
    cout << "\n";
    cout << "options:\n";
    cout << "  --debug   Turn on verbose debugging output\n";
    cout << "  --random-start-delay <millis>\n";
    cout << "            Wait a random amount of time between 0 and <millis>\n";
    cout << "            prior to starting each command\n";
    cout << "  --libssh-verbosity <level>\n";
    cout << "            Set libssh verbosity level to <level>.  Valid values\n";
    cout << "            are: none, warning, protocol, packet, and functions.\n";
    cout << "            Default value is protocol.\n";
    cout << endl;
    quick_exit(EXIT_FAILURE);
  }
}

/// @defgroup ssh ssh
/// @ingroup Tools
/// Mulit-host ssh automation tool.

int main(int argc, char **argv) {

  System::initialize();
  Config::properties = make_shared<Properties>();
  Config::properties->set("verbose", g_boo(false)); // used with AsyncComm 

  ReactorFactory::initialize(System::get_processor_count());

  string host_spec;
  string command;

  int start_delay {};
  for (int i=1; i<argc; i++) {
    if (!strcmp(argv[i], "--debug")) {
      SshSocketHandler::enable_debug();
      Config::properties->set("verbose", (gBool)true);
    } else if (!strcmp(argv[i], "--libssh-verbosity")) {
      i++;
      if (i == argc)
        dump_usage_and_exit();
      SshSocketHandler::set_libssh_verbosity(argv[i]);
      Config::properties->set("verbose", (gBool)true);
    }
    else if (!strcmp(argv[i], "--random-start-delay")) {
      i++;
      if (i == argc)
        dump_usage_and_exit();
      start_delay = atoi(argv[i]);
    }
    else if (host_spec.empty())
      host_spec = argv[i];
    else if (command.empty())
      command.append(argv[i]);
    else {
      command.append(" ");
      command.append(argv[i]);
    }
  }

  if (command.empty())
    dump_usage_and_exit();

  vector<string> hosts;
  try {
    hosts = HostSpecification(host_spec);
  }
  catch (Exception &e) {
    cout << "Invalid host specification: " << e.what() << endl;
    quick_exit(EXIT_FAILURE);
  }

  (void)signal(SIGINT,  sig);
  (void)signal(SIGQUIT, sig);

  ssh_threads_set_callbacks(SshThreadsCallbacks::get());
  ssh_init();

  g_handlers.reserve(hosts.size());

  for (auto & host : hosts)
    g_handlers.push_back(make_shared<SshSocketHandler>(host));

  auto now = chrono::system_clock::now();
  auto deadline = now + std::chrono::seconds(30);

  {
    lock_guard<mutex> lock(g_mutex);
    g_handlers_created = true;
    if (g_cancelled)
      quick_exit(EXIT_FAILURE);
  }

  // Wait for connections
  for (auto & handler : g_handlers) {
    if (!handler->wait_for_connection(deadline)) {
      handler->dump_log(cerr);
      quick_exit(EXIT_FAILURE);
    }
  }

  {
    lock_guard<mutex> lock(g_mutex);
    if (g_cancelled)
      quick_exit(EXIT_FAILURE);
  }

  vector<string> failed_hosts;

  // Issue commands

  if (start_delay) {
    map<chrono::time_point<chrono::steady_clock>, SshSocketHandlerPtr> start_map;
    chrono::time_point<chrono::steady_clock> start_time;
    for (auto & handler : g_handlers) {
      start_time = chrono::steady_clock::now()
        + std::chrono::milliseconds(Random::number32() % start_delay);
      start_map[start_time] = handler;
    }
    chrono::time_point<chrono::steady_clock> now;
    for (auto & entry : start_map) {
      now = chrono::steady_clock::now();
      this_thread::sleep_for(chrono::duration_cast<std::chrono::milliseconds>(entry.first-now));
      if (!entry.second->issue_command(command)) {
        entry.second->dump_log(cerr);
        failed_hosts.push_back(entry.second->hostname());        
        entry.second.reset();
      }
    }
  }
  else {
    for (auto & handler : g_handlers) {
      if (!handler->issue_command(command)) {
        handler->dump_log(cerr);
        failed_hosts.push_back(handler->hostname());
        handler.reset();
      }
    }
  }

  // Wait for commands to complete
  for (auto & handler : g_handlers) {
    if (handler) {
      handler->set_terminal_output(true);
      if (!handler->wait_for_command_completion()) {
        handler->dump_log(cerr);
        failed_hosts.push_back(handler->hostname());
      }
      handler->set_terminal_output(false);
    }
  }

  if (!failed_hosts.empty()) {
    cerr << "Command failed on hosts:  ";
    bool first = true;
    for (auto & host : failed_hosts) {
      if (first)
        first = false;
      else
        cerr << ",";
      cerr << host;
    }
    cerr << endl << flush;
    quick_exit(2);
  }

  cout << flush;

  quick_exit(EXIT_SUCCESS);
}
