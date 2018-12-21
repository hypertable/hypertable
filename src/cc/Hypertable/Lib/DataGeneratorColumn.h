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

#ifndef Hypertable_Lib_DataGeneratorColumn_h
#define Hypertable_Lib_DataGeneratorColumn_h

#include "Cell.h"
#include "DataGeneratorRandom.h"
#include "DataGeneratorRowComponent.h"
#include "DataGeneratorQualifier.h"

#include <Common/FileUtils.h>
#include <Common/String.h>
#include <Common/WordStream.h>

#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

extern "C" {
#include <limits.h>
#include <stdlib.h>
}

using namespace Hypertable;
using namespace std;

namespace Hypertable {

  class ColumnSpec {
  public:
    ColumnSpec() {}
    QualifierSpec qualifier;
    int size {-1};
    int order {RANDOM};
    std::string source;
    std::string cooked_source;
    std::string column_family;
    unsigned seed {1};
    std::string distribution;
    bool word_stream {};
    bool to_stdout {};
    bool fixed {};
  };

  class Column : public ColumnSpec {
  public:
    Column(ColumnSpec &spec) : ColumnSpec(spec) {
      if (spec.qualifier.type != -1)
        m_qualifiers.push_back( QualifierFactory::create(spec.qualifier) );
      m_next_qualifier = m_qualifiers.size();
    }
    virtual ~Column() { }
    virtual bool next() = 0;
    virtual std::string &qualifier() = 0;
    virtual const char *value() = 0;
    virtual uint32_t value_len() = 0;
  protected:
    std::vector<Qualifier *> m_qualifiers;
    size_t m_next_qualifier;
  };

  class ColumnString : public Column {
  public:
    ColumnString(ColumnSpec &spec, bool keys_only = false)
      : Column(spec), m_keys_only(keys_only), m_first_offset(0), m_size(0),
        m_second_offset(0) {
      std::string s = source;
      if (s.empty())
        s = cooked_source;

      if (word_stream) {
        if (s.empty())
          HT_FATAL("Source file not specified for word stream");
        if (size == -1)
          HT_FATAL("Size not specified for word stream");
        m_word_stream = std::make_shared<WordStream>(s, seed, size, order == RANDOM);
      }
      else {

        if (s.empty()) {
          m_value_data_len = size;
          if (!fixed)
            m_value_data_len *= 50;
          m_value_data.reset( new char [ m_value_data_len+1 ] );
          random_fill_with_chars((char *)m_value_data.get(), m_value_data_len);
          ((char *)m_value_data.get())[m_value_data_len] = 0;
          m_source = (const char *)m_value_data.get();

        }
        else {
          m_source = (const char *)FileUtils::mmap(s, &m_value_data_len);
          HT_ASSERT(m_value_data_len >= size);
        }
        m_value = m_source;
        m_value_data_len -= size;
        if (cooked_source.empty())
          m_render_buf.reset( new char [size * 2 + 1] );
        else
          m_render_buf.reset( new char [1024 * 10] );
      }
    }

    virtual ~ColumnString() { }

    virtual bool next() {
      // "cooked mode": we have two pointers. move the second pointer forward.
      // if it reaches eof then restart at the beginning, and move the first
      // pointer forward
      off_t offset {};
      const char *p;
      size_t first_word_size {};
      size_t second_word_size {};
      if (!cooked_source.empty()) {
        m_cooked.clear();
        p = m_source + m_first_offset;
        while (*p && *p != '\n') {
          p++;
          first_word_size++;
        }
        p = m_source + m_second_offset;
        while (*p && *p != '\n') {
          p++;
          second_word_size++;
        }
        // add the first word
        m_cooked.insert(m_cooked.end(), m_source + m_first_offset, 
                m_source + m_first_offset + first_word_size);
        m_cooked += " ";
        m_cooked.insert(m_cooked.end(), m_source + m_second_offset, 
                m_source + m_second_offset + second_word_size);

        // update the offsets
        m_second_offset += second_word_size + 1;
        if (m_second_offset >= m_value_data_len) {
          m_second_offset = 0;
          m_first_offset += first_word_size + 1;
          if (m_first_offset >= m_value_data_len)
            m_first_offset = 0;
        }
        m_size = m_cooked.size();
      }
      else if (!m_word_stream) {
        if (!fixed)
          offset = random_int32((int32_t)m_value_data_len);
      }

      if (m_qualifiers.empty())
        m_next_qualifier = 0;
      else
        m_next_qualifier = (m_next_qualifier + 1) % m_qualifiers.size();

      if (m_next_qualifier == 0 && !m_keys_only) {
        if (m_word_stream) {
          m_value = m_word_stream->next();
        }
        else {
          if (to_stdout) {
            if (!m_cooked.empty())
              m_value = m_cooked.c_str();
            else
              m_value = m_source + offset;
          }
          else if (!fixed) {
            const char *src = m_source + offset;
            if (!m_cooked.empty())
              src = m_cooked.c_str();
            char *dst = m_render_buf.get();
            for (size_t i=0; i<(size_t)value_len(); i++) {
              if (*src == '\n') {
                *dst++ = '\\';
                *dst++ = 'n';
              }
              else if (*src == '\t') {
                *dst++ = '\\';
                *dst++ = 't';
              }
              else if (*src == '\0') {
                *dst++ = '\\';
                *dst++ = '0';
              }
              else
                *dst++ = *src;
              src++;
            }
            *dst = 0;
            m_value = m_render_buf.get();
          }
        }
      }
      if (m_qualifiers.empty())
        return false;
      m_qualifiers[m_next_qualifier]->next();
      if (m_next_qualifier == (m_qualifiers.size()-1))
        return false;
      return true;
    }

    virtual std::string &qualifier() {
      if (m_qualifiers.empty())
        return m_qualifier;
      return m_qualifiers[m_next_qualifier]->get();
    }

    virtual const char *value() {
      return m_value;
    }

    virtual uint32_t value_len() {
      if (word_stream)
	return strlen(m_value);
      return m_size ? m_size : size;
    }

  private:
    bool m_keys_only;
    const char *m_value;
    std::string m_qualifier;
    boost::shared_array<char> m_render_buf;
    boost::shared_array<const char> m_value_data;
    const char *m_source;
    off_t m_value_data_len;
    off_t m_first_offset;
    size_t m_size;
    off_t m_second_offset;
    std::string m_cooked;
    WordStreamPtr m_word_stream;
  };

}

#endif // Hypertable_Lib_DataGeneratorColumn_h
