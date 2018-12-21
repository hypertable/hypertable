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
 * Properties Parser, Definition and Parsing for Properties
 * the options are configured in Config.h.
 */

#ifndef Common_PropertiesParser_h
#define Common_PropertiesParser_h

#include <iostream>
#include <iomanip>
#include <fstream>

#include <vector>
#include <string>
#include <map>

#include <Common/Property.h>



namespace Hypertable {


// Config::cfg(Int32tSafe default_value, true)
template<typename T>
inline Property::ValuePtr cfg(T v = 0, bool skippable=false, bool guarded=false) {
  return new Property::Value(v, skippable, guarded);
}

/* cfg methods for types
*  @param v The default Value and a Type
*/
Property::ValuePtr boo(bool v);
Property::ValuePtr i16(uint16_t v);
Property::ValuePtr i32(int32_t v);
Property::ValuePtr i64(int64_t v);
Property::ValuePtr f64(double v);
Property::ValuePtr str(String v);
Property::ValuePtr strs(Strings v);
Property::ValuePtr i64s(Int64s v);
Property::ValuePtr f64s(Doubles v);
Property::ValuePtr enum_ext(EnumExt v);
/* cfg methods for guarded types
*  @param v The default Value and a Type
*/
Property::ValuePtr g_boo(bool v);
Property::ValuePtr g_i32(int32_t v);
Property::ValuePtr g_strs(Strings v);
Property::ValuePtr g_enum_ext(gEnumExt v);

/* cfg methods for types, a skippable option
*  if no option parsed it is skipped
*/
Property::ValuePtr boo();
Property::ValuePtr i16();
Property::ValuePtr i32();
Property::ValuePtr i64();
Property::ValuePtr f64();
Property::ValuePtr str();
Property::ValuePtr strs();
Property::ValuePtr i64s();
Property::ValuePtr f64s();

/* cfg methods for guarded  types, a skippable option
*  if no option parsed it is skipped
*/
Property::ValuePtr g_strs();


 namespace Config {

  /** @addtogroup Common
   *  @{
   */
  
struct ParserOpt {
  Property::ValuePtr  value;
  Strings             aliases;
  String              desc;
};

class ParserConfig {
  typedef std::map<String, ParserOpt> Map;
  typedef std::pair<String, ParserOpt> MapPair;
  typedef std::pair<Map::iterator, bool> InsRet;

  typedef std::pair<int, String> PosPair;
  typedef std::vector<PosPair> Positions;

  public:

    ParserConfig();
    ParserConfig(String usage, int line_len=0);

    /* populate from other Parser Config */
    ParserConfig &add(ParserConfig other_cfg);

    virtual ~ParserConfig(){}

    /* Method to add option */
    ParserConfig &add(const String &names, Property::ValuePtr vptr, 
                                                      String description);

    ParserConfig &operator()(const String &name, Property::ValuePtr vptr, 
                                                      String description) {
      return add(name, vptr, description);
    }

    ParserConfig &add_options(){
      return *this;
    }

    ParserConfig &add_options(const String &name, Property::ValuePtr vptr, 
                                                      String description){
      return add(name, vptr, description);
    }

    ParserConfig &add(const String &names, String description){
      return add(names, cfg(true, true)->zero_token(), description);
    }

    ParserConfig &operator()(const String &name, String description) {
      return add(name, description);
    }
    
    /* Method to add_pos option */
    ParserConfig &add_pos(const String s, int pos);

    ParserConfig &operator()(const String s, int pos) {
      return add_pos(s, pos);
    }

    operator ParserConfig*(){
      return this;
    }

    Map get_map(){
      return m_cfg;
    }

    String get_usage(){
      return m_usage;
    }

    Positions get_positions(){
      return m_poss;
    }

    bool has(String &name);

    Property::ValuePtr get_default(const String name);

    void print(std::ostream& os);

    const String position_name(int n);

  private:
    String    m_usage;
    Map       m_cfg;
    int       m_line_length = 0;
    Positions m_poss;
};

std::ostream& operator<<(std::ostream& os,  ParserConfig& cfg);




class Parser{
  public:
    typedef std::map<String, Property::ValuePtr> Options;
    typedef std::pair<String, Property::ValuePtr> OptPair;

    Parser(std::ifstream &in, ParserConfig cfg, bool unregistered=false);

    Strings arsg_to_strings(int argc, char *argv[]){
      Strings raw_strings;
      for(int n=1;n<argc;n++)  raw_strings.push_back(String(argv[n]));
      return raw_strings;
    }
    Parser(int argc, char *argv[], ParserConfig *main, 
           ParserConfig *hidden=nullptr, bool unregistered=false)
      : Parser(arsg_to_strings(argc, argv), main, hidden, unregistered){      
    }

    Parser(Strings raw_strings, ParserConfig *main, 
           ParserConfig *hidden=nullptr, bool unregistered=false);

    void parse_line(String line);

    void set_pos_parse(const String name, String value);

    bool parse_opt(String s);
    
    void print(std::ostream& os);

    void make_options();

    // convert, validate and add property to options
    void add_opt(const String name, Property::ValuePtr p, Strings raw_opt);
    
    void print_options(std::ostream& os);
    
    Options get_options(){
      return m_opts;
    }

    ParserConfig get_cfg(){
      return m_cfg;
    }
    virtual ~Parser(){}
  private:
    typedef std::pair<String, Strings> Pair;
    typedef std::map<String, Strings> Map;
    typedef std::pair<Map::iterator, bool> InsRet;
    Map raw_opts;

    ParserConfig m_cfg;
    bool m_unregistered;
    Options m_opts;
    
};

std::ostream& operator<<(std::ostream& os,  Parser& prs);


/** @} */
} // namespace Config
} // namespace Hypertable

#endif // Common_PropertiesParser_h
