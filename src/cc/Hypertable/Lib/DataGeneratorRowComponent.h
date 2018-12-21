/* -*- c++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

#ifndef Hypertable_Lib_DataGeneratorRowComponent_h
#define Hypertable_Lib_DataGeneratorRowComponent_h

#include "DataGeneratorRandom.h"

#include <Hypertable/Lib/Cell.h>

#include <Common/DiscreteRandomGeneratorFactory.h>
#include <Common/Random.h>
#include <Common/String.h>

extern "C" {
#include <limits.h>
#include <stdlib.h>
}

#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <memory>

using namespace std;

namespace Hypertable {

  enum Type { INTEGER, STRING, TIMESTAMP };

  enum Order { RANDOM, ASCENDING };

  class RowComponentSpec {
  public:
    RowComponentSpec() : type(-1), order(-1), value_count(0), seed((unsigned)-1) { }
    int type {};
    int order {};
    std::string format;
    std::string min;
    std::string max;
    unsigned length_min {};
    unsigned length_max {};
    uint64_t value_count {};
    unsigned seed {};
    std::string distribution;
  };

  class RowComponent : public RowComponentSpec {
  public:
    RowComponent(RowComponentSpec &spec) : RowComponentSpec(spec) {
      if (order == RANDOM) {
        m_rng = DiscreteRandomGeneratorFactory::create(spec.distribution);
        m_rng->set_seed(seed);
      }
    }
    virtual ~RowComponent() { }
    virtual bool next() = 0;
    virtual void render(String &dst) = 0;
  protected:
    DiscreteRandomGeneratorPtr m_rng;
  };

  class RowComponentString : public RowComponent {
  public:
    RowComponentString(RowComponentSpec &spec) : RowComponent(spec) {

      if (length_min == 0 && length_max == 0) {
        cout << "ERROR: length.min and/or length.max must be specified for row component type 'string'" << endl;
        std::quick_exit(EXIT_FAILURE);
      }
      else if (length_max < length_min) {
        cout << "ERROR: length.max must be less than length.min for row component" << endl;
        std::quick_exit(EXIT_FAILURE);
      }

      if (order != RANDOM) {
        cout << "ERROR: 'random' is the only currently supported row component type" << endl;
        std::quick_exit(EXIT_FAILURE);
      }

      m_render_buf.reset( new char [length_max+1] );
      m_render_buf.get()[length_max] = 0;
    }
    virtual ~RowComponentString() { }
    virtual bool next() {
      random_fill_with_chars(m_render_buf.get(), length_max);
      return false;
    }
    virtual void render(String &dst) {
      dst.append((const char *)m_render_buf.get());
    }
  private:
    std::unique_ptr<char[]> m_render_buf;
  };

  class RowComponentInteger : public RowComponent {
  public:
    RowComponentInteger(RowComponentSpec &spec) : RowComponent(spec) {
      m_min = (min != "") ? strtoll(min.c_str(), 0, 0) : 0;
      if (m_min < 0)
        m_min = 0;
      m_max = (max != "") ? strtoll(max.c_str(), 0, 0) : std::numeric_limits<int64_t>::max();
      HT_ASSERT(m_min < m_max);
      m_next = m_max;
      if (order == RANDOM) {
        m_rng->set_pool_min(m_min);
        m_rng->set_pool_max(m_max);
        if (value_count)
          m_rng->set_value_count(value_count);
      }
      if (format.length() == 0)
        m_render_buf = new char [ 32 ];
      else {
        char tbuf[32];
        int total_len = snprintf(tbuf, 32, format.c_str(), (Lld)1);
        m_render_buf = new char [ total_len + 32 ];
      }
    }
    virtual ~RowComponentInteger() { 
      delete [] m_render_buf;
    }
    virtual bool next() {
      bool rval = true;
      if (order == ASCENDING) {
        if (m_next == m_max) {
          m_next = m_min;
          rval = false;
        }
        m_next++;
      }
      else if (order == RANDOM) {
        m_next = m_rng->get_sample();
        rval = false;
      }
      else
        HT_FATAL("invalid order");
      if (format != "")
        sprintf(m_render_buf, format.c_str(), (Lld)m_next);
      else
        sprintf(m_render_buf, "%lld", (Lld)m_next);
      return rval;
    }
    virtual void render(String &dst) {
      dst += (const char *)m_render_buf;
    }
  private:
    int64_t m_min, m_max, m_next;
    char *m_render_buf;
  };

  class RowComponentTimestamp : public RowComponent {
  public:
    RowComponentTimestamp(RowComponentSpec &spec) : RowComponent(spec) {
      struct tm tmval;
      if (format != "") {
        if (min != "") {
          strptime(min.c_str(), format.c_str(), &tmval);
          m_min = mktime(&tmval);
        }
        else
          m_min = 0;
        if (max != "") {
          strptime(max.c_str(), format.c_str(), &tmval);
          m_max = mktime(&tmval);
        }
        else
          m_max = std::numeric_limits<time_t>::max();
      }
      else {
        if (min != "") {
          strptime(min.c_str(), "%F %T", &tmval);
          m_min = mktime(&tmval);
        }
        else
          m_min = 0;
        if (max != "") {
          strptime(max.c_str(), "%F %T", &tmval);
          m_max = mktime(&tmval);
        }
        else
          m_max = std::numeric_limits<time_t>::max();
      }
      HT_ASSERT(m_min < m_max);
      m_next = m_max;
      if (order == RANDOM) {
        m_rng->set_pool_min(m_min);
        m_rng->set_pool_max(m_min);
        if (value_count)
          m_rng->set_value_count(value_count);
      }

      m_render_buf_len = 32;
      if (format.length() == 0)
        m_render_buf = new char [ m_render_buf_len ];
      else {
        char tbuf[32];
        int total_len = snprintf(tbuf, 32, format.c_str(), (Lld)1);
        m_render_buf_len = total_len + 32;
        m_render_buf = new char [ m_render_buf_len ];
      }

    }
    virtual ~RowComponentTimestamp() { 
      delete [] m_render_buf;
    }
    virtual bool next() {
      bool rval = true;
      if (order == ASCENDING) {
        if (m_next == m_max) {
          m_next = m_min;
          rval = false;
        }
        m_next++;
      }
      else if (order == RANDOM) {
        m_next = (time_t)m_rng->get_sample();
        rval = false;
      }
      else
        HT_FATAL("invalid order");
      struct tm tm_val;
      const char *cformat = (format == "") ? "%F %T" : format.c_str();
      localtime_r(&m_next, &tm_val);
      strftime(m_render_buf, m_render_buf_len, cformat, &tm_val);
      return rval;
    }
    virtual void render(String &dst) {
      dst += (const char *)m_render_buf;
    }
  private:
    time_t m_min, m_max, m_next;
    char *m_render_buf;
    int m_render_buf_len;
  };

}

#endif // Hypertable_Lib_DataGeneratorRowComponent_h
