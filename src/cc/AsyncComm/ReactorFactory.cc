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

#include "HandlerMap.h"
#include "ReactorFactory.h"
#include "ReactorRunner.h"

#include <Common/Config.h>
#include <Common/SystemInfo.h>

#include <cassert>

extern "C" {
#include <signal.h>
}

using namespace Hypertable;
using namespace std;

std::vector<ReactorPtr> ReactorFactory::ms_reactors;
boost::thread_group ReactorFactory::ms_threads;
default_random_engine ReactorFactory::rng {1};
mutex ReactorFactory::ms_mutex;
atomic<int> ReactorFactory::ms_next_reactor(0);
bool ReactorFactory::ms_epollet = true;
bool ReactorFactory::use_poll = false;
bool ReactorFactory::proxy_master = false;
gBoolPtr ReactorFactory::verbose {};

void ReactorFactory::initialize(uint16_t reactor_count) {
  lock_guard<mutex> lock(ms_mutex);
  if (!ms_reactors.empty())
    return;
  ReactorPtr reactor;
  ReactorRunner rrunner;
  ReactorRunner::handler_map = make_shared<HandlerMap>();
  signal(SIGPIPE, SIG_IGN);
  assert(reactor_count > 0);

#if defined(__linux__)
  if (System::os_info().version_major < 2 ||
      System::os_info().version_minor < 6 ||
      (System::os_info().version_major == 2 &&
       System::os_info().version_minor == 6 &&
       System::os_info().version_micro < 17))
    ms_epollet = false;
  if (System::os_info().version_major < 2 ||
      System::os_info().version_minor < 5)
    use_poll = true;
#endif

  if (Config::properties->has("Comm.UsePoll") &&
      Config::properties->get_bool("Comm.UsePoll"))
    use_poll = true;

  ms_reactors.reserve(reactor_count+2);
  for (uint16_t i=0; i<reactor_count+2; i++) {
    reactor = make_shared<Reactor>();
    ms_reactors.push_back(reactor);
    rrunner.set_reactor(reactor);
    ms_threads.create_thread(rrunner);
  }
}

void ReactorFactory::destroy() {
  ReactorRunner::shutdown = true;
  for (size_t i=0; i<ms_reactors.size(); i++)
    ms_reactors[i]->poll_loop_interrupt();
  ms_threads.join_all();
  ms_reactors.clear();
  ReactorRunner::handler_map = 0;
}

void ReactorFactory::join() {
  ms_threads.join_all();
}
