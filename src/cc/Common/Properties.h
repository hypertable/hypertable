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

/** @file
 * Program options handling.
 * Based on boost::program_options, the options are configured in Config.h.
 */

#ifndef Common_Properties_h
#define Common_Properties_h

#include <vector>
#include <string>
#include <mutex>
#include <Common/Error.h>
#include <Common/Logger.h>
#include <Common/Property.h>
#include <Common/PropertiesParser.h>


// convenience/abbreviated accessors
#define HT_PROPERTIES_ABBR_ACCESSORS() \
  inline bool get_bool(const String &name)  { \
    return get<bool>(name); } \
  inline String get_str(const String &name)  { \
    return get<String>(name); } \
  inline Strings get_strs(const String &name)  { \
    return get<Strings>(name); } \
  inline uint16_t get_i16(const String &name)  { \
    return get<uint16_t>(name); } \
  inline int32_t get_i32(const String &name)  { \
    return get<int32_t>(name); } \
  inline int64_t get_i64(const String &name)  { \
    return get<int64_t>(name); } \
  inline Int64s get_i64s(const String &name)  { \
    return get<Int64s>(name); } \
  inline double get_f64(const String &name)  { \
    return get<double>(name); } \
  inline Doubles get_f64s(const String &name)  { \
    return get<Doubles>(name); } \
  inline bool get_bool(const String &name, bool default_value) \
     { return get<bool>(name, default_value); } \
  inline String get_str(const String &name, String &default_value) \
     { return get<String>(name, static_cast<String>(default_value)); } \
  inline String get_str(const String &name, String default_value) \
     { return get<String>(name, default_value); } \
  inline Strings get_strs(const String &name, Strings default_value) \
     { return get<Strings>(name, default_value); } \
  inline uint16_t get_i16(const String &name, uint16_t default_value) \
     { return get<uint16_t>(name, default_value); } \
  inline int32_t get_i32(const String &name, int32_t default_value) \
     { return get<int32_t>(name, default_value); } \
  inline int64_t get_i64(const String &name, int64_t default_value) \
     { return get<int64_t>(name, default_value); } \
  inline Int64s get_i64s(const String &name, Int64s &default_value) \
     { return get<Int64s>(name, default_value); } \
  inline double get_f64(const String &name, double default_value) \
     { return get<double>(name, default_value); } \
  inline Doubles get_f64s(const String &name, Doubles default_value) \
     { return get<Doubles>(name, default_value); }
  /*     \
  inline bool get_bool(const String &name, bool default_value) \
    const { return get<bool>(name, default_value); } \
  inline String get_str(const String &name, String default_value) \
    const { return get<String>(name, default_value); } \
  inline Strings get_strs(const String &name, Strings &default_value) \
    const { return get<Strings>(name, default_value); } \
  inline uint16_t get_i16(const String &name, uint16_t default_value) \
    const { return get<uint16_t>(name, default_value); } \
  inline int32_t get_i32(const String &name, int32_t default_value) \
    const { return get<int32_t>(name, default_value); } \
  inline int64_t get_i64(const String &name, int64_t default_value)  \
    const { return get<int64_t>(name, default_value); } \
  inline Int64s get_i64s(const String &name, Int64s default_value) \
    const { return get<Int64s>(name, default_value); } \
  inline double get_f64(const String &name, double default_value) \
    const { return get<double>(name, default_value); } \
  inline Doubles get_f64s(const String &name, Doubles default_value) \
    const { return get<Doubles>(name, default_value); }
  */
 

namespace Hypertable {


/** @addtogroup Common
 *  @{
 */

typedef Config::ParserConfig PropertiesDesc;


/**
 * Manages a collection of program options.
 *
 * Implements getters and setters, aliases and default values.
 */
class Properties {
  typedef std::map<String, Property::ValuePtr> Map;
  typedef std::pair<String, Property::ValuePtr> MapPair;
  typedef std::pair<Map::iterator, bool> InsRet;
  typedef std::map<String, String> AliasMap;
  
public:
  /** Default constructor; creates an empty set */
  Properties() {  }

  /** Constructor; load properties from a filename
   *
   * @param filename The name of the file with the properties
   * @param desc A Property description with valid properties, default values
   * @param allow_unregistered If true, unknown/unregistered properties are
   *        accepted
   */
  Properties(const String &filename, PropertiesDesc &desc,
             bool allow_unregistered = false) {
    load(filename, desc, allow_unregistered);
  }

  /**
   * Loads the parsed options to properties
   *
   * @param opts onfig::Parser::Options
   * @param only_guarded update only huarded property
   */
  void load_from(Config::Parser::Options opts, bool only_guarded = false);

  /**
   * Loads a configuration file with properties
   *
   * @param filename The name of the configuration file
   * @param desc A property description
   * @param allow_unregistered If true, unknown/unregistered properties are
   *        accepted
   */
  void load(const String &filename, PropertiesDesc &desc,
	  bool allow_unregistered = false);

  /**
   * Loads a configuration files by property name
  *
   * @param names_prop The name of property name
   * @param desc A property description
   * @param allow_unregistered If true, unknown/unregistered properties are
   *        accepted
   */
  void load_files_by(const String &names_prop, PropertiesDesc &desc, 
	  bool allow_unregistered = false);
    
  /**
   * ReLoads a configuration file with properties
   *
   * @param filename The name of the configuration file
   * @param desc A property description
   * @param allow_unregistered If true, unknown/unregistered properties are
   *        accepted
   */
  String reload(const String &filename, PropertiesDesc &desc,
	  bool allow_unregistered = false);

  /**
   * Parses command line arguments
   *
   * @param argc The argument count (from main)
   * @param argv The argument array (from main)
   * @param desc The options description
   * @param hidden The hidden options description
   * @param allow_unregistered If true, unknown/unregistered properties are
   *        accepted
   * @throw Error::CONFIG_INVALID_ARGUMENT on error
   */
  void parse_args(int argc, char *argv[], PropertiesDesc &desc,
              PropertiesDesc *hidden = 0, bool allow_unregistered = false);

  /**
   * Parses command line arguments
   *
   * @param args A vector of Strings with the command line arguments
   * @param desc The options description
   * @param hidden The hidden options description
   * @param allow_unregistered If true, unknown/unregistered properties are
   *        accepted
   * @throw Error::CONFIG_INVALID_ARGUMENT on error
   */
  void parse_args(const std::vector<String> &args, PropertiesDesc &desc,
              PropertiesDesc *hidden = 0, bool allow_unregistered = false);

  /**
   * Get the ptr of property value. Throws if name is not defined.
   * 
   * @param name The name of the property
   * @throw Error::CONFIG_GET_ERROR if the requested property is not defined
   * @return Property::ValuePtr
   */
  Property::ValuePtr get_value_ptr(const String &name) {
    try{
      return m_map.at(name);
    }
    catch (std::exception &e) {
      HT_THROWF(Error::CONFIG_GET_ERROR, "getting value of '%s': %s",
                name.c_str(), e.what());
    }
    // 
    // return nullptr;
  }

  /**
   * Get the ptr of property value type. Throws if name is not defined.
   * Property::ValueDef<int32_t>* threads = props->get_type_ptr<int32_t>("threads");
   * 
   * @param name The name of the property
   * @throw Error::CONFIG_GET_ERROR if the requested property is not defined
   * @return Property::ValueDef<T>*
   */
  template <typename T>
  Property::ValueDef<T>* get_type_ptr(const String &name) {
    return (Property::ValueDef<T>*)get_value_ptr(name)->get_type_ptr();
  }

  /**
   * Get the property value ptr
   *
   * @param name The name of the property
   * @throw Error::CONFIG_GET_ERROR if the requested property is not defined
   * @return T*
   */
  template <typename T>
  T* get_ptr(const String &name) {
    return get_value_ptr(name)->get_ptr<T>();
  }
  
  /**
   * Get the String representation of property name 
   *
   * @param name The name of the property
   * @throw Error::CONFIG_GET_ERROR if the requested property is not defined
   * @return const String
   */
  const String str(const String &name) {
    try {
      return get_value_ptr(name)->str();
    }
    catch (std::exception &e) {
      HT_THROWF(Error::CONFIG_GET_ERROR, "getting value of '%s': %s",
                name.c_str(), e.what());
    }
  }
  /**
   * Get the value of option of type T. Throws if option is not defined.
   *
   * @param name The name of the property
   * @throw Error::CONFIG_GET_ERROR if the requested property is not defined
   */
  template <typename T>
  T get(const String &name) {
    try {
      return get_value_ptr(name)->get<T>();
    }
    catch (std::exception &e) {
      HT_THROWF(Error::CONFIG_GET_ERROR, "getting value of '%s': %s",
                name.c_str(), e.what());
    }
  }
  
  /**
   * Get the value of option of type T by preference. Throws if option is not defined.
   *
   * @param namee The names of preference for the property
   * @throw Error::CONFIG_GET_ERROR if the requested property is not defined
   */
  template <typename T>
  T get_pref(Strings names){
    for(String name : names)
      if (has(name))
        return get<T>(name);
    HT_THROWF(Error::CONFIG_GET_ERROR, "getting pref value by '%s'",
                                        format_list(names).c_str());
  }

  /**
   * Get the value of option of type T. Returns supplied default value if
   * not found. Try use the first form in usual cases and supply default
   * values in the config descriptions, as it validates the name and is less
   * error prone.
   * If no value, sets the default value to properties 
   *
   * @param name The name of the property
   * @param default_value The default value to return if not found
   */
  template <typename T>
  T get(const String &name, T default_value) {
    try {
      Map::const_iterator it = m_map.find(name);

      if (it != m_map.end())
        return (*it).second->get<T>();
      
      set(name, default_value);
    
      return get<T>(name);
    }
    catch (std::exception &e) {
      HT_THROWF(Error::CONFIG_GET_ERROR, "getting value of '%s': %s",
                name.c_str(), e.what());
    }
  }
  /*
  template <typename T>
  const T get(const String &name, T &default_value)  {
    T v = get(name, default_value);
    return std::as_const(v);
  }
  */

  /**
   * Check whether a property is by default value
   *
   * @param name The name of the property
   * @return true if the value is default
   */
  bool defaulted(const String &name) {
    return get_value_ptr(name)->is_default();
  }

  /** Check whether a property exists
   *
   * @param name The name of the property
   * @return true if the property exists
   */
  bool has(const String &name) {
    return m_map.count(name);
  }
  /*
  bool has(const String &name) const {
    return m_map.count(name);
  }
  */

  HT_PROPERTIES_ABBR_ACCESSORS()

  /**
   * Add property to the map of type T
   *
   * @param name The name of the property
   * @param v The Property::ValueDef<T> the property
   * @return An iterator to the new item
   */
  template<typename T>
  InsRet add(const String &name, T v) {
    return m_map.insert(MapPair(name, new Property::Value(v)));
  }
  /**
   * Add property to the map
   *
   * @param name The name of the property
   * @param v The Property::ValuePtr of the property
   * @return An iterator to the new item
   */
  InsRet add(const String &name, Property::ValuePtr v) {
    return m_map.insert(MapPair(name, v));
  }

  /**
   * Set a property in the map, create or update, of T type
   *
   * @param name The name of the property
   * @param v The value of the property, name need to correspond to Value Type
   */
  template<typename T>
  void set(const String &name, T v, bool defaulted=false)  {
    InsRet r = add(name, v);
    if (!r.second){
      (*r.first).second = new Property::Value(v);
    } else 
      (*r.first).second->set_value(v);
    if(defaulted)
      (*r.first).second->default_value();
  }

  /**
   * Set a property in the map, create or update, by Property::ValueDef<T> type
   *
   * @param name The name of the property
   * @param v The Property::ValueDef<T>
   */
  template<typename T>
  void set(const String &name, Property::ValueDef<T> v)  {
    InsRet r = add(name, v.get_value());
    if (!r.second){
      (*r.first).second = new Property::Value(v.get_value());
    } else 
      (*r.first).second->set_value(v.get_value());
  }

  /**
   * Set a property in the map, create or update, from Property::ValuePtr
   *
   * @param name The name of the property
   * @param v The Property::ValuePtr
   */
  void set(const String &name, Property::ValuePtr p) {
    Property::ValuePtr p_set;
    if(m_map.count(name)==0){
      p_set = Property::make_new(p);
      
      if(p->is_default())
        p_set->default_value();
      
      if(p->is_guarded())
        p_set->guarded(true);
        
      add(name, p_set);

    } else if(!p->is_default()) {
      p_set = get_value_ptr(name);
      p_set->set_value_from(p);
      p_set->default_value(false);
    }
  }

  /**
   * Remove a property from the map
   *
   * @param name The name of the property
   */
  void remove(const String &name) {
    m_map.erase(name);
  }

  /**
   * Setup an alias for a property.
   *
   * The primary property has higher priority, meaning when
   * aliases are sync'ed the primary value can override secondary value
   *
   * @param primary The primary property name
   * @param secondary The secondary property name
   */
  void alias(const String &primary, const String &secondary);

  /**
   * Returns all property names
   *
   * @param names reference to vector to hold names of all properties
   */
  void get_names(std::vector<String> &names) {
    for (Map::const_iterator it = m_map.begin(); it != m_map.end(); it++)
      names.push_back((*it).first);
  }

  /**
   * Prints keys and values of the configuration map
   *
   * @param out The output stream
   * @param include_default If true then default values are included
   */
  void print(std::ostream &out, bool include_default = false);

  /**
   * Append to String keys and values of the configuration map for print
   *   
   * @param out The String to append 
   * @param include_default If true then default values are included
   */
  void append_to_string(String &out, bool include_default = false);

private:

  /** The map containing all properties */
  Map m_map;
  
  /** A map with all aliases */
  AliasMap m_alias_map;
};

 typedef std::shared_ptr<Properties> PropertiesPtr;



/** Helper class to access parts of the properties.
 *
 * This snippet extracts all properties that start with
 * "Hypertable.RangeServer." and uses the SubProperty to access
 * "Hypertable.RangeServer.Range.SplitSize". 
 *
 *     SubProperties cfg(props, "Hypertable.RangeServer.");
 *     i64_t foo = cfg.get_i64("Range.SplitSize");
 */
class SubProperties {
public:
  /** Constructor
   *
   * @param props The original Properties object
   * @param prefix The common prefix of all sub-properties
   */
  SubProperties(PropertiesPtr &props, const String &prefix)
    : m_props(props), m_prefix(prefix) {
  }

  /** Returns the full name of a sub-property
   *
   * @param name The name of the sub-property
   * @return The full name (prefix + name)
   */
  String full_name(const String &name) const {
    return m_prefix + name;
  }

  /**
   * Calls Properties::get for a sub-property.
   *
   * @param name The name of the sub-property
   */
  template <typename T>
  T get(const String &name) {
    return m_props->get<T>(full_name(name));
  }

  /**
   * Calls Properties::get for a sub-property.
   *
   * @param name The name of the sub-property
   * @param default_value The default value to return if not found
   */
  template <typename T>
  T get(const String &name, T default_value) {
    return m_props->get<T>(full_name(name), default_value);
  }

  /**
   * Check whether a sub-property has a default value
   *
   * @param name The name of the sub-property
   * @return true if the value is default
   */
  bool defaulted(const String &name) {
    return m_props->defaulted(full_name(name));
  }

  /**
   * Check whether a sub-property exists
   *
   * @param name The name of the sub-property
   * @return true if the property exists
   */
  bool has(const String &name) {
    return m_props->has(full_name(name));
  }

  HT_PROPERTIES_ABBR_ACCESSORS()

private:
  /** Reference to the original Properties object */
  PropertiesPtr m_props;

  /** The common prefix of all sub-properties */
  String m_prefix;
};

/** @} */

}
#endif // Common_Properties_h
