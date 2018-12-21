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

/// @file
/// Definitions for AccessGroupSpec.
/// This file contains type definitions for AccessGroupSpec, a class
/// representing an access group specification.

#include <Common/Compat.h>
#include "AccessGroupSpec.h"

#include <Common/Config.h>
#include <Common/XmlParser.h>

#include <boost/algorithm/string.hpp>

#include <mutex>
#include <stack>
#include <utility>

using namespace Hypertable;
using namespace Property;
using namespace std;

namespace {

  mutex desc_mutex;
  bool desc_inited = false;

  PropertiesDesc 
	  compressor_desc("  bmz|lzo|quicklz|zlib|snappy|zstd|none [compressor_options]\n\n"
		  "compressor_options"),
    bloomfilter_desc("  rows|rows+cols|none [bloomfilter_options]\n\n"
                      "  Default bloom filter is defined by the config property:\n"
                      "  Hypertable.RangeServer.CellStore.DefaultBloomFilter.\n\n"
                      "bloomfilter_options");

  PropertiesDesc compressor_hidden_desc, bloomfilter_hidden_desc;

  void init_schema_options_desc() {
    lock_guard<mutex> lock(desc_mutex);

    if (desc_inited)
      return;

    compressor_desc.add_options()
	    ("ultra,20", "Highest setting (probably slower) for zstd")
	    ("best,9", "Highest setting (probably slower) for zlib, zstd")
      ("normal", "Normal setting for zlib, zstd")
      ("fp-len", i16(19), "Minimum fingerprint length for bmz")
      ("offset", i16(0), "Starting fingerprint offset for bmz")
      ;
    compressor_hidden_desc.add_options()
      ("compressor-type", str(), 
       "Compressor type (bmz|lzo|quicklz|zlib|snappy|zstd|none)")
       ("compressor-type", 1);

    bloomfilter_desc.add_options()
      ("bits-per-item", f64(), "Number of bits to use per item "
       "for the Bloom filter")
      ("num-hashes", i32(), "Number of hash functions to use "
       "for the Bloom filter")
      ("false-positive", f64(0.01), "Expected false positive "
       "probability for the Bloom filter")
      ("max-approx-items", i32(1000), "Number of cell store "
       "items used to guess the number of actual Bloom filter entries")
      ;
    bloomfilter_hidden_desc.add_options()
      ("bloom-filter-mode", str(), "Bloom filter mode (rows|rows+cols|none)")
      ("bloom-filter-mode", 1);
      // ("bloom-filter-mode", eNum<ConfBloomFilterMode>(0)
      ;
  
    desc_inited = true;
  }

  void validate_compressor(const std::string &compressor) {
    if (compressor.empty())
      return;

    try {
      init_schema_options_desc();
      PropertiesPtr props = make_shared<Properties>();
      vector<std::string> args;
      boost::split(args, compressor, boost::is_any_of(" \t"));
      HT_TRY("parsing compressor spec",
             props->parse_args(args, compressor_desc, &compressor_hidden_desc));
    }
    catch (Exception &e) {
      HT_THROWF(Error::SCHEMA_PARSE_ERROR, "Invalid compressor spec - %s",
                compressor.c_str());
    }
  }

  void validate_bloomfilter(const std::string &bloomfilter) {
    if (bloomfilter.empty())
      return;

    try {
      init_schema_options_desc();
      PropertiesPtr props = make_shared<Properties>();
      vector<std::string> args;

      boost::split(args, bloomfilter, boost::is_any_of(" \t"));
      HT_TRY("parsing bloom filter spec",
             props->parse_args(args, bloomfilter_desc, &bloomfilter_hidden_desc));
    }
    catch (Exception &e) {
      HT_THROWF(Error::SCHEMA_PARSE_ERROR, "Invalid bloom filter spec - %s",
                bloomfilter.c_str());
    }
  }

} // local namespace


void AccessGroupOptions::set_replication(int16_t replication) {
  m_replication = replication;
  m_isset.set(REPLICATION);
}

bool AccessGroupOptions::is_set_replication() const {
  return m_isset.test(REPLICATION);
}

void AccessGroupOptions::set_blocksize(int32_t blocksize) {
  m_blocksize = blocksize;
  m_isset.set(BLOCKSIZE);
}

bool AccessGroupOptions::is_set_blocksize() const {
  return m_isset.test(BLOCKSIZE);
}

void AccessGroupOptions::set_compressor(const std::string &compressor) {
  validate_compressor(compressor);
  m_compressor = compressor;
  m_isset.set(COMPRESSOR);
}

bool AccessGroupOptions::is_set_compressor() const {
  return m_isset.test(COMPRESSOR);
}

void AccessGroupOptions::set_bloom_filter(const std::string &bloomfilter) {
  validate_bloomfilter(bloomfilter);
  m_bloomfilter = bloomfilter;
  m_isset.set(BLOOMFILTER);
}

bool AccessGroupOptions::is_set_bloom_filter() const {
  return m_isset.test(BLOOMFILTER);
}

void AccessGroupOptions::set_in_memory(bool value) {
  m_in_memory = value;
  m_isset.set(IN_MEMORY);
}

bool AccessGroupOptions::is_set_in_memory() const {
  return m_isset.test(IN_MEMORY);
}

void AccessGroupOptions::merge(const AccessGroupOptions &other) {
  if (!is_set_replication() && other.is_set_replication())
    set_replication(other.get_replication());
  if (!is_set_blocksize() && other.is_set_blocksize())
    set_blocksize(other.get_blocksize());
  if (!is_set_compressor() && other.is_set_compressor())
    set_compressor(other.get_compressor());
  if (!is_set_bloom_filter() && other.is_set_bloom_filter())
    set_bloom_filter(other.get_bloom_filter());
  if (!is_set_in_memory() && other.is_set_in_memory())
    set_in_memory(other.get_in_memory());
}

namespace {
  
  class XmlParserAccessGroupOptions : public XmlParser {
  public:
    XmlParserAccessGroupOptions(AccessGroupOptions *ops, const char *s,
                                 int len) : XmlParser(s,len), m_options(ops) {}

    void end_element(const XML_Char *name, const string &content) override {
      if (!strcasecmp(name, "Replication"))
        m_options->set_replication(content_to_i16(name, content));
      else if (!strcasecmp(name, "BlockSize"))
        m_options->set_blocksize(content_to_i32(name, content));
      else if (!strcasecmp(name, "Compressor"))
        m_options->set_compressor(content);
      else if (!strcasecmp(name, "BloomFilter"))
        m_options->set_bloom_filter(content);
      else if (!strcasecmp(name, "InMemory"))
        m_options->set_in_memory(content_to_bool(name, content));
      else if (!m_element_stack.empty())
        HT_THROWF(Error::SCHEMA_PARSE_ERROR,
                  "Unrecognized AccessGroup option element (%s)", name);
    }
  private:
    AccessGroupOptions *m_options;
  };
  
}

void AccessGroupOptions::parse_xml(const char *s, int len) {
  XmlParserAccessGroupOptions parser(this, s, len);
  parser.parse();
}

const std::string AccessGroupOptions::render_xml(const std::string &line_prefix) const {
  std::string xstr;
  if (is_set_replication())
    xstr += format("%s<Replication>%d</Replication>\n",
                   line_prefix.c_str(), m_replication);
  if (is_set_blocksize())
    xstr += format("%s<BlockSize>%d</BlockSize>\n",
                   line_prefix.c_str(), m_blocksize);
  if (is_set_compressor())
    xstr += format("%s<Compressor>%s</Compressor>\n",
                   line_prefix.c_str(), m_compressor.c_str());
  if (is_set_bloom_filter())
    xstr += format("%s<BloomFilter>%s</BloomFilter>\n",
                   line_prefix.c_str(), m_bloomfilter.c_str());
  if (is_set_in_memory())
    xstr += format("%s<InMemory>%s</InMemory>\n",
                   line_prefix.c_str(), m_in_memory ? "true" : "false");
  return xstr;
}

const std::string AccessGroupOptions::render_hql() const {
  std::string hstr;
  if (is_set_replication())
    hstr += format(" REPLICATION %d", m_replication);
  if (is_set_blocksize())
    hstr += format(" BLOCKSIZE %d", m_blocksize);
  if (is_set_compressor())
    hstr += format(" COMPRESSOR \"%s\"", m_compressor.c_str());
  if (is_set_bloom_filter())
    hstr += format(" BLOOMFILTER \"%s\"", m_bloomfilter.c_str());
  if (is_set_in_memory())
    hstr += format(" IN_MEMORY %s", m_in_memory ? "true" : "false");
  return hstr;
}

bool AccessGroupOptions::operator==(const AccessGroupOptions &other) const {
  return (m_isset == other.m_isset &&
          m_replication == other.m_replication &&
          m_blocksize == other.m_blocksize &&
          m_compressor == other.m_compressor &&
          m_bloomfilter == other.m_bloomfilter &&
          m_in_memory == other.m_in_memory);
}


void AccessGroupOptions::parse_bloom_filter(const std::string &spec, PropertiesPtr &props) {

  init_schema_options_desc();
  
  vector<std::string> args;
  boost::split(args, spec, boost::is_any_of(" \t"));
  HT_TRY("parsing bloom filter spec",
         props->parse_args(args, bloomfilter_desc, &bloomfilter_hidden_desc));
  
  std::string mode = props->get_str("bloom-filter-mode");
  // property name used with enum and string!!, 
  // TODO: EnumExt with enum validation and repr
  if (mode == "none" || mode == "disabled")
    props->set("bloom-filter-mode", BLOOM_FILTER_DISABLED);
  else if (mode == "rows" || mode == "row")
    props->set("bloom-filter-mode", BLOOM_FILTER_ROWS);
  else if (mode == "rows+cols" || mode == "row+col"
           || mode == "rows-cols" || mode == "row-col"
           || mode == "rows_cols" || mode == "row_col")
    props->set("bloom-filter-mode", BLOOM_FILTER_ROWS_COLS);
  else
    HT_THROWF(Error::BAD_SCHEMA, "unknown bloom filter mode: '%s'",
                 mode.c_str());
}

AccessGroupSpec::~AccessGroupSpec() {
  for (auto cf_spec : m_columns)
    delete cf_spec;
}

void AccessGroupSpec::set_name(const std::string &name) {
  m_name = name;
}

bool AccessGroupSpec::clear_generation_if_changed(AccessGroupSpec &original) {
  using namespace std::rel_ops;
  bool changed {};
  for (auto cf : m_columns) {
    auto orig_cf = original.get_column(cf->get_name());
    if (orig_cf == nullptr) {
      m_generation = 0;
      changed = true;
    }
    else if (*cf != *orig_cf) {
      if (cf->get_option_time_order_desc() != orig_cf->get_option_time_order_desc())
        HT_THROWF(Error::INVALID_OPERATION,
                  "Modificaton of TIME ORDER option for column '%s' is not allowed.",
                  cf->get_name().c_str());
      if (cf->get_option_counter() != orig_cf->get_option_counter())
        HT_THROWF(Error::INVALID_OPERATION,
                  "Modificaton of COUNTER option for column '%s' is not allowed.",
                  cf->get_name().c_str());
      m_generation = 0;
      changed = true;
    }

  }
  if (!changed) {
    for (auto cf : original.columns()) {
      if (get_column(cf->get_name()) == nullptr) {
        m_generation = 0;
        changed = true;
        break;
      }
    }
    if (m_options != original.m_options) {
      if (m_options.get_in_memory() != original.get_option_in_memory())
        HT_THROWF(Error::INVALID_OPERATION, "Modificaton of IN_MEMORY option "
                  "of access group '%s' is not allowed.", m_name.c_str());
      m_generation = 0;
      changed = true;
    }
    if (m_defaults != original.m_defaults) {
      m_generation = 0;
      changed = true;
    }
  }
  return changed;
}

void AccessGroupSpec::set_option_replication(int16_t replication) {
  if (!m_options.is_set_replication() ||
      m_options.get_replication() != replication)
    m_generation = 0;
  m_options.set_replication(replication);
}

int16_t AccessGroupSpec::get_option_replication() const {
  return m_options.get_replication();
}

void AccessGroupSpec::set_option_blocksize(int32_t blocksize) {
  if (!m_options.is_set_blocksize() ||
      m_options.get_blocksize() != blocksize)
    m_generation = 0;
  m_options.set_blocksize(blocksize);
}

int32_t AccessGroupSpec::get_option_blocksize() const {
  return m_options.get_blocksize();
}

void AccessGroupSpec::set_option_compressor(const std::string &compressor) {
  if (!m_options.is_set_compressor() ||
      m_options.get_compressor() != compressor)
    m_generation = 0;
  m_options.set_compressor(compressor);
}

const std::string AccessGroupSpec::get_option_compressor() const {
  return m_options.get_compressor();
}

void AccessGroupSpec::set_option_bloom_filter(const std::string &bloomfilter) {
  if (!m_options.is_set_bloom_filter() ||
      m_options.get_bloom_filter() != bloomfilter)
    m_generation = 0;
  m_options.set_bloom_filter(bloomfilter);
}

const std::string &AccessGroupSpec::get_option_bloom_filter() const {
  return m_options.get_bloom_filter();
}

void AccessGroupSpec::set_option_in_memory(bool value) {
  if (!m_options.is_set_in_memory() ||
      m_options.get_in_memory() != value)
    m_generation = 0;
  m_options.set_in_memory(value);
}

bool AccessGroupSpec::get_option_in_memory() const {
  return m_options.get_in_memory();
}

void AccessGroupSpec::set_default_max_versions(int32_t max_versions) {
  if (!m_defaults.is_set_max_versions() ||
      m_defaults.get_max_versions() != max_versions)
    m_generation = 0;
  m_defaults.set_max_versions(max_versions);
}

int32_t AccessGroupSpec::get_default_max_versions() const {
  return m_defaults.get_max_versions();
}

void AccessGroupSpec::set_default_ttl(time_t ttl) {
  if (!m_defaults.is_set_ttl() || m_defaults.get_ttl() != ttl)
    m_generation = 0;
  m_defaults.set_ttl(ttl);
}

time_t AccessGroupSpec::get_default_ttl() const {
  return m_defaults.get_ttl();
}

void AccessGroupSpec::set_default_time_order_desc(bool value) {
  if (!m_defaults.is_set_time_order_desc() ||
      m_defaults.get_time_order_desc() != value)
    m_generation = 0;
  m_defaults.set_time_order_desc(value);
}

bool AccessGroupSpec::get_default_time_order_desc() const {
  return m_defaults.get_time_order_desc();
}

void AccessGroupSpec::set_default_counter(bool value) {
  if (!m_defaults.is_set_counter() || m_defaults.get_counter() != value)
    m_generation = 0;
  m_defaults.set_counter(value);
}

bool AccessGroupSpec::get_default_counter() const {
  return m_defaults.get_counter();
}

void AccessGroupSpec::add_column(ColumnFamilySpec *cf) {
  cf->merge_options(m_defaults);
  if (cf->get_access_group().empty())
    cf->set_access_group(m_name);
  HT_ASSERT(cf->get_access_group() == m_name);
  m_columns.push_back(cf);
}

ColumnFamilySpec *AccessGroupSpec::replace_column(ColumnFamilySpec *new_cf) {
  ColumnFamilySpec *old_cf = 0;
  ColumnFamilySpecs new_columns;
  new_cf->merge_options(m_defaults);
  for (auto cf : m_columns) {
    if (cf->get_name() == new_cf->get_name()) {
      old_cf = cf;
      new_columns.push_back(new_cf);
    }
    else
      new_columns.push_back(cf);
  }
  HT_ASSERT(old_cf);
  m_columns.swap(new_columns);
  return old_cf;
}

ColumnFamilySpec *AccessGroupSpec::remove_column(const std::string &name) {
  ColumnFamilySpec *old_cf = 0;
  ColumnFamilySpecs new_columns;
  for (auto cf : m_columns) {
    if (cf->get_name() == name)
      old_cf = cf;
    else
      new_columns.push_back(cf);
  }
  m_columns.swap(new_columns);
  return old_cf;
}

void AccessGroupSpec::drop_column(const std::string &name) {
  for (auto cf : m_columns) {
    if (cf->get_name() == name) {
      cf->set_generation(0);
      cf->set_deleted(true);
      cf->set_name(format("!%d", cf->get_id()));
      break;
    }
  }
}

ColumnFamilySpec *AccessGroupSpec::get_column(const std::string &name) {
  for (auto cf : m_columns)
    if (cf->get_name() == name)
      return cf;
  return nullptr;
}

bool AccessGroupSpec::operator==(const AccessGroupSpec &other) const {
  return (m_name == other.m_name &&
          m_options == other.m_options &&
          m_defaults == other.m_defaults &&
          m_columns.size() == other.m_columns.size());
  // !!! fixme - need to compare column family specs as well
}

namespace {
  
  class XmlParserAccessGroupSpec : public XmlParser {
  public:
    XmlParserAccessGroupSpec(AccessGroupSpec *spec, const char *s, int len) :
      XmlParser(s,len,{"Options","ColumnFamilyDefaults","ColumnFamily"}),
      m_spec(spec) {}

    void start_element(const XML_Char *name, const XML_Char **atts) override {
      if (m_element_stack.empty()) {
        for (int i=0; atts[i] != 0; i+=2) {
          if (!strcasecmp(atts[i], "name"))
            m_spec->set_name(atts[i+1]);
          // begin backward-compatibility
          else if (!strcasecmp(atts[i], "inMemory"))
            m_spec->set_option_in_memory(content_to_bool(atts[i], atts[i+1]));
          else if (!strcasecmp(atts[i], "counter"))
            m_spec->set_default_counter(content_to_bool(atts[i], atts[i+1]));
          else if (!strcasecmp(atts[i], "replication"))
            m_spec->set_option_replication(content_to_i16(atts[i], atts[i+1]));
          else if (!strcasecmp(atts[i], "blksz"))
            m_spec->set_option_blocksize(content_to_i32(atts[i], atts[i+1]));
          else if (!strcasecmp(atts[i], "compressor"))
            m_spec->set_option_compressor(atts[i+1]);
          else if (!strcasecmp(atts[i], "bloomFilter"))
            m_spec->set_option_bloom_filter(atts[i+1]);
          // end backward-compatibility
          else
            HT_THROWF(Error::SCHEMA_PARSE_ERROR,
                      "Unrecognized attribute (%s) in AccessGroup element", atts[i]);
        }
      }
    }

    void end_element(const XML_Char *name, const string &content) override {
      if (!strcasecmp(name, "Generation"))
        m_spec->set_generation(content_to_i64(name, content));
      else if (!m_element_stack.empty())
        HT_THROWF(Error::SCHEMA_PARSE_ERROR,
                  "Unrecognized AccessGroup element (%s)", name);
    }

    void sub_parse(const XML_Char *name, const char *s, int len) override {
      if (!strcasecmp(name, "Options")) {
        AccessGroupOptions options;
        options.parse_xml(s, len);
        m_spec->merge_options(options);
      }
      else if (!strcasecmp(name, "ColumnFamilyDefaults")) {
        ColumnFamilyOptions defaults;
        defaults.parse_xml(s, len);
        m_spec->merge_defaults(defaults);
      }
      else if (!strcasecmp(name, "ColumnFamily")) {
        ColumnFamilySpec *cf_spec = new ColumnFamilySpec();
        cf_spec->parse_xml(s, len);
        m_spec->add_column(cf_spec);
      }
    }

  private:
    AccessGroupSpec *m_spec;
  };
  
}

void AccessGroupSpec::parse_xml(const char *base, int len) {
  XmlParserAccessGroupSpec parser(this, base, len);
  parser.parse();
}


const std::string AccessGroupSpec::render_xml(const std::string &line_prefix,
                                              bool with_ids) const {
  string output;
  stack<string> prefix;
  prefix.push(line_prefix);
  output += prefix.top() + format("<AccessGroup name=\"%s\">\n",
                                  get_name().c_str());
  prefix.push(prefix.top() + "  ");
  if (with_ids)
    output += prefix.top() +
      format("<Generation>%lld</Generation>\n", (Lld)m_generation);
  output += prefix.top() + "<Options>\n";
  prefix.push(prefix.top() + "  ");
  output += m_options.render_xml(prefix.top());
  prefix.pop();
  output += prefix.top() + "</Options>\n";
  output += prefix.top() + "<ColumnFamilyDefaults>\n";
  prefix.push(prefix.top() + "  ");
  output += m_defaults.render_xml(prefix.top());
  prefix.pop();
  output += prefix.top() + "</ColumnFamilyDefaults>\n";
  for (auto cf_spec : m_columns)
    output += cf_spec->render_xml(prefix.top(), with_ids);
  prefix.pop();
  output += prefix.top() + "</AccessGroup>\n";
  return output;
}

namespace {
  const std::string maybe_quote(const string &name) {
    auto valid_char = [](char c) { return isalnum(c); };
    if (!isalpha(name[0]) ||
        find_if_not(name.begin(), name.end(), valid_char) != name.end())
      return String("'") + name + "'";
    return name;
  }
}


const std::string AccessGroupSpec::render_hql() const {
  string output;
  output = "  ACCESS GROUP ";
  output += maybe_quote(m_name);
  output += " (";
  bool first = true;
  for (auto cf_spec : m_columns) {
    if (cf_spec->get_deleted())
      continue;
    if (!first)
      output += ", ";
    else
      first = false;
    output += maybe_quote(cf_spec->get_name());
  }
  output += ")";
  output += m_options.render_hql();    
  output += m_defaults.render_hql();    
  return output;
}
