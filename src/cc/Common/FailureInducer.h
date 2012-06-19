/** -*- c++ -*-
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

#ifndef HYPERTABLE_FAILUREINDUCER_H
#define HYPERTABLE_FAILUREINDUCER_H

#include "HashMap.h"
#include "Mutex.h"
#include "String.h"
#include "StringExt.h"

namespace Hypertable {

  class FailureInducer {
  public:
    static FailureInducer *instance;
    static bool enabled() { return (bool)instance; }
    void parse_option(String option);
    void maybe_fail(const String &label);
    bool failure_signalled(const String &label);
    void clear();

  private:
    void parse_option_single(String option);
    struct failure_inducer_state {
      uint32_t iteration;
      uint32_t trigger_iteration;
      int failure_type;
      int error_code;
      int pause_millis;
    };
    typedef hash_map<String, failure_inducer_state *> StateMap;
    Mutex m_mutex;
    StateMap m_state_map;
  };

}

#define HT_MAYBE_FAIL(_label_) \
  if (Hypertable::FailureInducer::enabled()) { \
    Hypertable::FailureInducer::instance->maybe_fail(_label_); \
  }

#define HT_MAYBE_FAIL_X(_label_, _exp_) \
  if (Hypertable::FailureInducer::enabled() && (_exp_)) { \
    Hypertable::FailureInducer::instance->maybe_fail(_label_); \
  }

#define HT_FAILURE_SIGNALLED(_label_) \
  Hypertable::FailureInducer::enabled() && \
    Hypertable::FailureInducer::instance->failure_signalled(_label_)

#endif // HYPERTABLE_FAILUREINDUCER_H
