/*
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

#include <Common/Compat.h>

#include "DataGenerator.h"
#include "DataGeneratorRandom.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <cstdlib>

extern "C" {
#include <limits.h>
#include <strings.h>
}

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace boost;

DataGeneratorIterator::DataGeneratorIterator(DataGenerator *generator)
  : m_generator(generator), m_keys_only(generator->m_keys_only),
    m_amount(0), m_count(0), m_last_data_size(0) {
  RowComponent *row_comp;
  Column *column;

  for (size_t i=0; i<generator->m_row_component_specs.size(); i++) {
    if (generator->m_row_component_specs[i].type == Type::INTEGER)
      row_comp = new RowComponentInteger( generator->m_row_component_specs[i] );
    else if (generator->m_row_component_specs[i].type == Type::STRING)
      row_comp = new RowComponentString( generator->m_row_component_specs[i] );
    else if (generator->m_row_component_specs[i].type == Type::TIMESTAMP)
      row_comp = new RowComponentTimestamp( generator->m_row_component_specs[i] );
    else
      HT_ASSERT(!"Unrecognized row component type");
    m_row_components.push_back(row_comp);
  }

  if (m_keys_only) {
    m_cell.value = (const ::uint8_t *)"";
    m_cell.value_len = 0;
  }

  for (size_t i=0; i<generator->m_column_specs.size(); i++) {
    column = new ColumnString( generator->m_column_specs[i], m_keys_only );
    m_columns.push_back(column);
  }
  m_next_column = m_columns.size() - 1;

  next();
}


void DataGeneratorIterator::next() {
  size_t compi = m_row_components.size();

  HT_ASSERT(compi > 0);

  if (m_columns.size() <= 1)
    m_next_column = 0;
  else
    m_next_column = random_int32(m_columns.size());

  do {
    compi--;
    if (m_row_components[compi]->next())
      break;
  } while (compi > 0);

  m_row.clear();

  for (size_t i=0; i<m_row_components.size(); i++)
    m_row_components[i]->render(m_row);

  m_cell.row_key = m_row.c_str();

  if (!m_columns.empty()) {
    m_columns[m_next_column]->next();
    m_cell.column_family = m_columns[m_next_column]->column_family.c_str();
    m_cell.column_qualifier = m_columns[m_next_column]->qualifier().c_str();
    if (!m_keys_only) {
      m_cell.value = (const ::uint8_t *)m_columns[m_next_column]->value();
      m_cell.value_len = m_columns[m_next_column]->value_len();
    }
    m_last_data_size = m_row.length() + strlen(m_cell.column_qualifier) + m_cell.value_len;
    m_amount += m_last_data_size;
  }
  else {
    m_last_data_size = m_row.length();
    m_amount += m_last_data_size;
  }
  m_count++;
}

DataGeneratorIterator& DataGeneratorIterator::operator++() {
  next();
  return *this;
}

DataGeneratorIterator& DataGeneratorIterator::operator++(int n) {
  next();
  for (int i=1; i<n; i++)
    next();
  return *this;
}



DataGenerator::DataGenerator(PropertiesPtr &props, bool keys_only) : m_props(props), m_keys_only(keys_only) {
  int rowkey_order;
  string rowkey_distribution;
  unsigned int rowkey_seed;
  string str;
  std::map<String, int> column_map;

  if (has("DataGenerator.MaxBytes"))
    m_max_bytes = get_i64("DataGenerator.MaxBytes");
  else
    m_max_bytes = m_props->get_i64("DataGenerator.MaxBytes", std::numeric_limits< ::int64_t >::max());

  if (has("DataGenerator.MaxKeys"))
    m_max_keys = get_i64("DataGenerator.MaxKeys");
  else
    m_max_keys = m_props->get_i64("DataGenerator.MaxKeys", std::numeric_limits< ::int64_t >::max());

  if (has("DataGenerator.Seed"))
    m_seed = get_i32("DataGenerator.Seed");
  else
    m_seed = m_props->get_i32("DataGenerator.Seed", 1);

  rowkey_order = parse_order( m_props->get_str("rowkey.order", "ascending") );
  rowkey_distribution = m_props->get_str("rowkey.distribution", "uniform");
  rowkey_seed = m_props->get_i32("rowkey.seed", 1);

  std::vector<String> names;
  string name;
  m_props->get_names(names);
  long index = 0;
  char *ptr, *tptr;

  for (size_t i=0; i<names.size(); i++) {
    if (starts_with(names[i], "rowkey.component.")) {
      index = strtol(names[i].c_str()+17, &ptr, 0);
      if (index < 0 || index > 100)
        HT_THROW(Error::SYNTAX_ERROR, format("Bad format for key %s", names[i].c_str()));
      if (m_row_component_specs.size() <= (size_t)index)
        m_row_component_specs.resize(index+1);
      if (!strcmp(ptr, ".type")) {
        str = m_props->get_str(names[i]);
        if (!strcasecmp(str.c_str(), "integer"))
          m_row_component_specs[index].type = Type::INTEGER;
        else if (!strcasecmp(str.c_str(), "string"))
          m_row_component_specs[index].type = Type::STRING;
        else if (!strcasecmp(str.c_str(), "timestamp"))
          m_row_component_specs[index].type = Type::TIMESTAMP;
        else
          HT_THROW(Error::SYNTAX_ERROR, format("Invalid rowkey component type - %s", str.c_str()));
      }
      else if (!strcmp(ptr, ".format")) {
        m_row_component_specs[index].format = m_props->get_str(names[i]);
        boost::trim_if(m_row_component_specs[index].format, boost::is_any_of("'\""));
      }
      else if (!strcmp(ptr, ".min")) {
        m_row_component_specs[index].min = m_props->get_str(names[i]);
      }
      else if (!strcmp(ptr, ".max")) {
        m_row_component_specs[index].max = m_props->get_str(names[i]);
      }
      else if (!strcmp(ptr, ".values")) {
        str = m_props->get_str(names[i]);
        m_row_component_specs[index].value_count = (uint64_t)strtoll(str.c_str(), 0, 0);
      }
      else if (ends_with(ptr, ".order")) {
        m_row_component_specs[index].order = parse_order( m_props->get_str(names[i]) );
      }
      else if (ends_with(ptr, ".distribution")) {
        m_row_component_specs[index].distribution = m_props->get_str(names[i]);
      }
      else if (ends_with(ptr, ".seed")) {
        str = m_props->get_str(names[i]);
        m_row_component_specs[index].seed = atoi(str.c_str());
      }
      else if (ends_with(ptr, ".length.min")) {
        str = m_props->get_str(names[i]);
        m_row_component_specs[index].length_min = atoi(str.c_str());
      }
      else if (ends_with(ptr, ".length.max")) {
        str = m_props->get_str(names[i]);
        m_row_component_specs[index].length_max = atoi(str.c_str());
      }
      else
        HT_THROW(Error::SYNTAX_ERROR, format("Invalid key - %s", names[i].c_str()));

    }
    else if (strstr(names[i].c_str(), ".qualifier.") || strstr(names[i].c_str(), ".value.")) {
      int columni;
      name = String("") + names[i];
      tptr = strchr((char *)name.c_str(), '.');
      *tptr++ = 0;

      std::map<String, int>::iterator iter = column_map.find((String)name.c_str());
      if (iter == column_map.end()) {
        columni = column_map.size();
        column_map[(String)name.c_str()] = columni;
        m_column_specs.push_back(ColumnSpec());
        m_column_specs[columni].column_family = name;
      }
      else
        columni = (*iter).second;

      str = m_props->get_str(names[i]);

      if (!strcmp(tptr, "qualifier.type")) {
        if (!strcasecmp(str.c_str(), "STRING"))
          m_column_specs[columni].qualifier.type = Type::STRING;
        else
          HT_THROW(Error::SYNTAX_ERROR, format("Unsupported type (%s) for '%s'",
                                               str.c_str(), names[i].c_str()));
      }
      else if (!strcmp(tptr, "qualifier.size")) {
        m_column_specs[columni].qualifier.size = atoi(str.c_str());
      }
      else if (!strcmp(tptr, "qualifier.charset")) {
        m_column_specs[columni].qualifier.charset = str;
        boost::trim_if(m_column_specs[columni].qualifier.charset, boost::is_any_of("'\""));
      }
      else if (!strcmp(tptr, "value.random")) {
        if (!strcasecmp(str.c_str(), "false"))
          m_column_specs[columni].order = ASCENDING;
        else if (!strcasecmp(str.c_str(), "true"))
          m_column_specs[columni].order = RANDOM;
        else
          HT_THROW(Error::SYNTAX_ERROR, format("Bad value for 'value.random'"));
      }
      else if (!strcmp(tptr, "value.seed")) {
        m_column_specs[columni].seed = atoi(str.c_str());
      }
      else if (!strcmp(tptr, "value.size")) {
        m_column_specs[columni].size = atoi(str.c_str());
      }
      else if (!strcmp(tptr, "value.source")) {
        m_column_specs[columni].source = str;
      }
      else if (!strcmp(tptr, "value.source.words")) {
        if (!strcasecmp(str.c_str(), "true"))
          m_column_specs[columni].word_stream = true;
        else if (!strcasecmp(str.c_str(), "false"))
          m_column_specs[columni].word_stream = false;
        else
          HT_THROW(Error::SYNTAX_ERROR, format("Bad value for 'value.source.words'"));
      }
      else if (!strcmp(tptr, "value.cooked-source")) {
        m_column_specs[columni].cooked_source = str;
      }
      else if (!strcmp(tptr, "value.fixed")) {
        if (!strcasecmp(str.c_str(), "false"))
          m_column_specs[columni].fixed = false;
        else if (!strcasecmp(str.c_str(), "true"))
          m_column_specs[columni].fixed = true;
        else
          HT_THROW(Error::SYNTAX_ERROR, format("Bad value for 'value.fixed'"));
      }

    }
  }

  if (!keys_only && m_column_specs.empty())
    HT_FATAL("No columns specified");

  for (size_t i=0; i<m_column_specs.size(); i++) {
    if (m_column_specs[i].qualifier.type != -1) {
      if (m_column_specs[i].qualifier.size == -1)
        HT_THROW(Error::SYNTAX_ERROR,
                 format("No qualifier size specified for column '%s'",
                        m_column_specs[i].column_family.c_str()));
      if (m_column_specs[i].qualifier.charset == "")
        HT_THROW(Error::SYNTAX_ERROR,
                 format("No qualifier charset specified for column '%s'",
                        m_column_specs[i].column_family.c_str()));
    }
    if (m_column_specs[i].size == -1)
      HT_THROW(Error::SYNTAX_ERROR,
               format("No value size specified for column '%s'",
                      m_column_specs[i].column_family.c_str()));
  }

  for (size_t i=0; i<m_row_component_specs.size(); i++) {
    if (m_row_component_specs[i].order == -1)
      m_row_component_specs[i].order = rowkey_order;
    if (m_row_component_specs[i].distribution == "")
      m_row_component_specs[i].distribution = rowkey_distribution;
    if (m_row_component_specs[i].seed == (unsigned)-1)
      m_row_component_specs[i].seed = rowkey_seed;
    if (m_row_component_specs[i].type == -1)
      HT_FATALF("Missing type for component %lu", (Lu)i);
    else if (m_row_component_specs[i].type == Type::INTEGER &&
             m_row_component_specs[i].format != "") {
      if (!strstr(m_row_component_specs[i].format.c_str(), "lld"))
        HT_FATALF("Format sequence (%s) must contain 'lld'",
                  m_row_component_specs[i].format.c_str());
    }
  }
}


int DataGenerator::parse_order(const string &str) {
  if (!strcasecmp(str.c_str(), "ascending"))
    return ASCENDING;
  else if (!strcasecmp(str.c_str(), "random"))
    return RANDOM;
  else
    HT_THROW(Error::SYNTAX_ERROR, format("Unsupported order - %s", str.c_str()));
}

