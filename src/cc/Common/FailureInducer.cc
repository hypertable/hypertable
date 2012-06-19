/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Compat.h"
#include "Common/String.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <vector>

extern "C" {
#include <poll.h>
}

#include "FailureInducer.h"
#include "Error.h"
#include "Logger.h"

using namespace Hypertable;

namespace {
  enum { 
    FAILURE_TYPE_EXIT, 
    FAILURE_TYPE_THROW,
    FAILURE_TYPE_SIGNAL,
    FAILURE_TYPE_PAUSE
  };
}

FailureInducer *FailureInducer::instance = 0;

void FailureInducer::parse_option(String option) {
  std::vector<String> args;
  boost::algorithm::split(args, option, boost::algorithm::is_any_of(";"));

  foreach (String &a, args)
    parse_option_single(a);
}

void FailureInducer::maybe_fail(const String &label) {
  ScopedLock lock(m_mutex);
  StateMap::iterator iter = m_state_map.find(label);
  if (iter != m_state_map.end()) {
    if ((*iter).second->iteration == (*iter).second->trigger_iteration) {
      if ((*iter).second->failure_type == FAILURE_TYPE_THROW) {
        uint32_t iteration = (*iter).second->iteration;
        int error_code = (*iter).second->error_code;
        delete (*iter).second;
        m_state_map.erase(iter);
        HT_THROW(error_code,
                 format("induced failure code '%d' '%s' iteration=%u",
                        error_code, label.c_str(), iteration));
      }
      else if ((*iter).second->failure_type == FAILURE_TYPE_PAUSE) {
	HT_INFOF("Induced pause at '%s' iteration=%u for %u milliseconds",
		 label.c_str(), (*iter).second->iteration,
		 (*iter).second->pause_millis);
	poll(0, 0, (*iter).second->pause_millis);
      }
      else {
        HT_ERRORF("induced failure code '%d' '%s' iteration=%u",
                 (*iter).second->error_code, (*iter).first.c_str(), 
                 (*iter).second->iteration);
        _exit(1);
      }
    }
    (*iter).second->iteration++;
  }
}

bool FailureInducer::failure_signalled(const String &label) {
  ScopedLock lock(m_mutex);
  StateMap::iterator iter = m_state_map.find(label);
  if (iter == m_state_map.end())
    return false;
  if ((*iter).second->failure_type == FAILURE_TYPE_SIGNAL
        && (*iter).second->iteration == (*iter).second->trigger_iteration)
      return true;
  (*iter).second->iteration++;
  return false;
}

void FailureInducer::clear() {
  ScopedLock lock(m_mutex);
  for (StateMap::iterator iter = m_state_map.begin(); 
          iter != m_state_map.end(); ++iter)
    delete iter->second;
  m_state_map.clear();
}

void FailureInducer::parse_option_single(String option) {
  char *istr = (char*)strchr(option.c_str(), ':');
  HT_ASSERT(istr != 0);
  *istr++ = 0;

  const char *failure_type = istr;
  istr = strchr(istr, ':');
  HT_ASSERT(istr != 0);
  *istr++ = 0;
  size_t failure_type_len = strlen(failure_type);
  failure_inducer_state *statep = new failure_inducer_state;

  if (!strcmp(failure_type, "exit"))
    statep->failure_type = FAILURE_TYPE_EXIT;
  else if (!strcmp(failure_type, "signal"))
    statep->failure_type = FAILURE_TYPE_SIGNAL;
  else if (boost::algorithm::starts_with(failure_type, "pause")) {
    statep->failure_type = FAILURE_TYPE_PAUSE;
    if (failure_type_len > 5 && failure_type[5] == '(') {
      const char *ptr = failure_type + 6;
      statep->pause_millis = (int)strtol(ptr, NULL, 0);
    }
  }
  else if (boost::algorithm::starts_with(failure_type, "throw")) {
    statep->failure_type = FAILURE_TYPE_THROW;
    statep->error_code = Error::INDUCED_FAILURE;
    if (failure_type_len > 5 && failure_type[5] == '(') {
      const char *error_code = failure_type + 6;
      statep->error_code = (int)strtol(error_code, NULL, 0);
    }
  }
  else
    HT_ASSERT(!"Unknown failure type");

  statep->iteration = 0;
  statep->trigger_iteration = atoi(istr);
  m_state_map[option.c_str()] = statep;
}

