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

#ifndef Hypertable_Lib_DataGeneratorQualifier_h
#define Hypertable_Lib_DataGeneratorQualifier_h

#include "Cell.h"
#include "DataGeneratorRandom.h"
#include "DataGeneratorRowComponent.h"

#include <Common/Logger.h>
#include <Common/String.h>

#include <boost/shared_array.hpp>

using namespace Hypertable;

namespace Hypertable {

  class QualifierSpec {
  public:
    QualifierSpec() : type(-1), order(RANDOM), size(-1) { }
    int type;
    int order;
    int size;
    std::string charset;
    unsigned seed;
    std::string distribution;
  };

  class Qualifier : public QualifierSpec {
  public:
    Qualifier(QualifierSpec &spec) : QualifierSpec(spec) { }
    virtual ~Qualifier() { }
    virtual bool next() = 0;
    virtual std::string &get() = 0;
  };

  class QualifierString : public Qualifier {
  public:
    QualifierString(QualifierSpec &spec) : Qualifier(spec) {
      HT_ASSERT(size > 0);
      m_render_buf.reset( new char [ size + 1 ] );
      ((char *)m_render_buf.get())[ size ] = 0;
    }
    virtual ~QualifierString() { }
    virtual bool next() {
      if (charset.length() > 0)
        random_fill_with_chars((char *)m_render_buf.get(), size, charset.c_str());
      else
        random_fill_with_chars((char *)m_render_buf.get(), size);
      m_qualifier = m_render_buf.get();
      return false;
    }
    virtual std::string &get() { return m_qualifier; }
  private:
    boost::shared_array<const char> m_render_buf;
    std::string m_qualifier;
  };

  class QualifierFactory {
  public:
    static Qualifier *create(QualifierSpec &spec) {
      if (spec.type == Type::STRING)
        return new QualifierString(spec);
      else
        HT_ASSERT(!"Invalid qualifier type");
    }
  };

}

#endif // Hypertable_Lib_DataGeneratorQualifier_h
