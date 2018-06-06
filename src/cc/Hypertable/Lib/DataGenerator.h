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

#ifndef Hypertable_Lib_DataGenerator_h
#define Hypertable_Lib_DataGenerator_h

#include "Cell.h"
#include "DataGeneratorRandom.h"
#include "DataGeneratorRowComponent.h"
#include "DataGeneratorQualifier.h"
#include "DataGeneratorColumn.h"

#include <Common/Config.h>
#include <Common/Random.h>
#include <Common/String.h>

#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

extern "C" {
#include <limits.h>
#include <stdlib.h>
}

namespace Hypertable {
  using namespace Hypertable::Config;
  using namespace std;

  struct DataGeneratorPolicy : Config::Policy {
    static void init_options() {
      allow_unregistered_options(true);
      file_desc().add_options()
        ("DataGenerator.DeletePercentage", i32(),
         "When generating update workload make this percentage of them deletes")
        ("DataGenerator.MaxBytes", i64(),
         "Maximum number of bytes of key and value data to generate")
        ("DataGenerator.MaxKeys", i64(),
         "Maximum number of keys to generate for query load")
        ("DataGenerator.Seed", i32()->default_value(1),
         "Pseudo-random number generator seed")
        ("rowkey.order", str()->default_value("random"), "Order in which to "
         "generate row keys (random, ascending)")
        ("rowkey.distribution", str()->default_value("uniform"),
         "Default rowkey distribution (uniform, zipf)")
        ("rowkey.seed", i32()->default_value(1),
         "Default rowkey random number generator seed")
        ("rowkey.component.<n>.type", str(),
         "Type of rowkey component <n> (string, integer)")
        ("rowkey.component.<n>.format", str(), "printf-style format string "
         "for rendering rowkey component <n>.")
        ("rowkey.component.<n>.distribution", str(),
         "Distribution for rowkey component <n>")
        ("rowkey.component.<n>.order", str(),
         "Order in which to generate row keys for component <n>")
        ("rowkey.component.<n>.min", str(),
         "Minimum value for rowkey component <n>.")
        ("rowkey.component.<n>.max", str(),
         "Maximum value for rowkey component <n>.")
        ("rowkey.component.<n>.length.min", str(),
         "Minimum length of randomly generated row component <n>.")
        ("rowkey.component.<n>.length.max", str(),
         "Maximum length of randomly generated row component <n>.")
        ("<column>.qualifier.type", str(), "Type of qualifier")
        ("<column>.qualifier.size", i32(), "Size of qualifier")
        ("<column>.qualifier.charset", str(),
         "Set of characters to use when generating string qualifiers")
        ("<column>.value.random", boo(), "Generate random values (default=true)")
        ("<column>.value.seed", i32(), "Pseudo-random generator seed")
        ("<column>.value.size", i32(), "Size of value")
        ("<column>.value.source", i32(), "Source file to pull value data from")
        ("<column>.value.source.words", i32(), "Interpret source as word stream; value.size treated as word count")
        ("<column>.value.fixed", boo(), "Used the same fixed value for each cell (default=false)")
        ;
      cmdline_hidden_desc().add(file_desc());
    }
  };

  class DataGenerator;

  /**
   * Provides an STL-style iterator on DataGenerator objects.
   */
  class DataGeneratorIterator : public iterator<forward_iterator_tag, Cell> {
  //class DataGeneratorIterator : public boost::iterator<forward_iterator_tag, Cell> { //boost 1.66.0?
  
    friend class DataGenerator;

  public:

    Cell& operator*() { return m_cell; }

    void next();
    DataGeneratorIterator& operator++();
    DataGeneratorIterator& operator++(int n);

    unsigned long last_data_size() { return m_last_data_size; }

    bool operator!=(const DataGeneratorIterator& other) const {
      if (other.m_count != std::numeric_limits< ::int64_t >::max())
        return m_count <= other.m_count;
      return m_amount < other.m_amount;
    }

  private:
    DataGeneratorIterator(DataGenerator *generator);
    DataGeneratorIterator(int64_t amount, int64_t count) : m_generator(0), m_amount(amount), m_count(count) {  }
    DataGenerator *m_generator;
    std::vector<RowComponent *> m_row_components;
    std::vector<Column *> m_columns;
    bool m_keys_only;
    Cell m_cell;
    int64_t m_amount;
    int64_t m_count;
    unsigned long m_last_data_size;
    std::string m_row;
    int32_t  m_next_column;
  };


  /**
   *
   */
  class DataGenerator {

  public:
    typedef DataGeneratorIterator iterator;
    friend class DataGeneratorIterator;

  public:
    DataGenerator(PropertiesPtr &props, bool keys_only=false);
    iterator begin() { random_generator_set_seed(m_seed); return DataGeneratorIterator(this); }
    iterator end() { return DataGeneratorIterator(m_max_bytes, m_max_keys); }
    int64_t get_max_bytes() { return m_max_bytes; }
    int64_t get_max_keys() { return m_max_keys; }

  protected:
    PropertiesPtr m_props;
    bool m_keys_only;
    int64_t  m_max_bytes;
    int64_t  m_max_keys;
    uint32_t m_seed;
    std::vector<RowComponentSpec> m_row_component_specs;
    std::vector<ColumnSpec> m_column_specs;

  private:
    int parse_order(const std::string &str);
  };

}

#endif // Hypertable_Lib_DataGenerator_h
