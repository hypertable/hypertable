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

#include <Common/PropertiesParser.h>

#include <Common/Compat.h>
#include <Common/Logger.h>
#include <Common/Error.h>


namespace Hypertable {


// cfg methods for types
Property::ValuePtr boo(bool v) {
  return cfg(v);
}
Property::ValuePtr i16(uint16_t v) {
  return cfg(v);
}
Property::ValuePtr i32(int32_t v) {
  return cfg(v);
}
Property::ValuePtr i64(int64_t v) {
  return cfg(v);
}
Property::ValuePtr f64(double v) {
  return cfg(v);
}
Property::ValuePtr str(String v) {
  return cfg(v);
}
Property::ValuePtr strs(Strings v) {
  return cfg(v);
}
Property::ValuePtr i64s(Int64s v) {
  return cfg(v);
}
Property::ValuePtr f64s(Doubles v) {
  return cfg(v);
}
Property::ValuePtr enum_ext(EnumExt v) {
  return cfg(v);
}

// cfg methods for guarded types
Property::ValuePtr g_boo(bool v) {
  return cfg((gBool)v, false, true);
}
Property::ValuePtr g_i32(int32_t v) {
  return cfg((gInt32t)v, false, true);
}
Property::ValuePtr g_strs(Strings v) {
  return cfg((gStrings)v, false, true);
}
Property::ValuePtr g_enum_ext(gEnumExt v) {
  return cfg(v, false, true);
}

// cfg methods for types, skipable options (no default set)
Property::ValuePtr boo() {
  return cfg(true, true);
}
Property::ValuePtr i16() {
  return cfg((uint16_t)0, true);
}
Property::ValuePtr i32() {
  return cfg((int32_t)0, true);
}
Property::ValuePtr i64() {
  return cfg((int64_t)0, true);
}
Property::ValuePtr f64() {
  return cfg((double)0, true);
}
Property::ValuePtr str() {
  return cfg(String(), true);
}
Property::ValuePtr strs() {
  return cfg(Strings(), true);
}
Property::ValuePtr i64s() {
  return cfg(Int64s(), true);
}
Property::ValuePtr f64s() {
  return cfg(Doubles(), true);
}

// cfg methods for guarded types, skipable options (no default set)
Property::ValuePtr g_strs() {
  return cfg((gStrings)Strings(), true);
}


namespace Config {

ParserConfig::ParserConfig(){            
  m_usage = "";   
}
ParserConfig::ParserConfig(String usage, int line_len){ 
  m_usage = usage;
  m_line_length = line_len;
}

ParserConfig& ParserConfig::add(ParserConfig other_cfg){
  m_usage.append(other_cfg.get_usage());
      
  for (auto & pos : other_cfg.get_positions()) 
    add_pos(pos.second, pos.first);

  for (const auto &kv : other_cfg.get_map()) {
    ParserOpt opt;
    opt.value = kv.second.value;
    opt.aliases = kv.second.aliases;
    opt.desc = kv.second.desc;
    m_cfg.insert(MapPair(kv.first, opt));
  }
  return *this;
}

ParserConfig& ParserConfig::add(const String &names, Property::ValuePtr vptr, 
                                                          String description){
  Strings aliases;
  std::istringstream f(names);
  String s;    
  while (getline(f, s, ',')) {
    s.erase(std::remove_if(s.begin(), s.end(),
     [](unsigned char x){return std::isspace(x);}), s.end());
    aliases.push_back(s);
  }
  String name = aliases.front();
  aliases.erase(aliases.begin());

  ParserOpt opt;
  opt.value = vptr;
  opt.aliases = aliases;
  opt.desc = description;
  InsRet r = m_cfg.insert(MapPair(name, opt));
  if (r.second) // overwrite previous
    (*r.first).second = opt;
  return *this;
}

ParserConfig& ParserConfig::add_pos(const String s, int pos){
  m_poss.push_back(std::make_pair(pos, s));
  return *this;
}

bool ParserConfig::has(String &name){
  for(MapPair info : get_map()){
    if(name.compare(info.first)==0)return true;
    for(String alias : info.second.aliases)
      if(name.compare(alias)==0){
        name = String(info.first);
        return true;
      }
  }
  return false;
}

Property::ValuePtr ParserConfig::get_default(const String name){
  return get_map().at(name).value;
}

void ParserConfig::print(std::ostream& os) {
  os << "\n" << m_usage << "\n";
  
  size_t tmp;
  size_t len_name=5;
  size_t len_desc=5;
  for (const auto &kv : get_map()) {
    tmp = (!kv.second.aliases.empty()? 
              format_list(kv.second.aliases).length()+2 : 0);
    if(len_name < tmp+kv.first.length())
      len_name = tmp+kv.first.length();
    if(len_desc < kv.second.desc.length())
      len_desc = kv.second.desc.length();
  }
  int offset_name = static_cast<int>(len_name)+2;
  int offset_desc = static_cast<int>(len_desc);

  for (const auto &kv : get_map()) {
    os << std::left << std::setw(2) << " "
       << std::left << std::setw(offset_name+2) << 
          format("--%s %s", kv.first.c_str(),
              (!kv.second.aliases.empty()? 
                format("-%s",format_list(kv.second.aliases).c_str()).c_str()
                : ""))
       << std::left << std::setw(offset_desc+2) << kv.second.desc
       << std::left << std::setw(2) << kv.second.value->str()
       << "\n";
  }
}

const String ParserConfig::position_name(int n){
  if(m_poss.empty()) return "";
  for (auto & pos : m_poss) if(pos.first==n) return pos.second;
  return "";
}

std::ostream& operator<<(std::ostream& os,  ParserConfig& cfg) {
  cfg.print(os);
  return os;
}




Parser::Parser(std::ifstream &in, ParserConfig cfg, bool unregistered){
  m_unregistered = unregistered;
  m_cfg = *(new ParserConfig());
  m_cfg.add(*cfg);
    
  size_t at;
  String group = "";
  String line, g_tmp;
  while(getline (in, line)){
    at = line.find_first_of("[");
    if(at == (size_t)0){

      at = line.find_first_of("]");      
      if(at!=std::string::npos) {

        g_tmp = line.substr(1, at-1);  
        if(!group.empty()){
          group.pop_back(); // remove a dot      
          if(g_tmp.compare(group+"=end")==0){ // an end of group  "[groupname=end]"
            group.clear();
            line.clear();
            continue;
          }
        }
        group = g_tmp+".";               // a start of group "[groupname]"
        line.clear();
        continue;
      }
    }

    parse_line(group+line);

  }
  make_options();
}

Parser::Parser(Strings raw_strings, ParserConfig *main, 
               ParserConfig *hidden, bool unregistered){
  m_unregistered = unregistered;

  m_cfg = *(new ParserConfig());
  m_cfg.add(*main);
  if(hidden != nullptr)
    m_cfg.add(*hidden);

  if(raw_strings.empty()) {
    make_options();
    return;
  }

  bool cfg_name;
  bool fill = false;
  String name, opt;
  int n=0;
  int len_o = raw_strings.size();

  for(String raw_opt: raw_strings){
    n++;

    // position based name with position's value
    name = m_cfg.position_name(n);
    if(!name.empty()){
      set_pos_parse(name, raw_opt);
      continue;
    }

    // if arg is a --name / -name
    if(!fill && raw_opt.find_first_of("-", 0, 1) != std::string::npos) {
        name = raw_opt.substr(1);
        if(name.find_first_of("-", 0, 1) != std::string::npos)
          name = name.substr(1);
        opt.append(name);

        // if not arg with name=Value 
        if(name.find_first_of("=")==std::string::npos){
          opt.append("=");

          if(!m_cfg.has(name) || !m_cfg.get_default(name)->is_zero_token()){
            if(len_o > n){
              fill = true;
              continue;
            }
          }
          opt.append("1"); // zero-token true default 
        }
    } else 
        opt.append(raw_opt); 
    
    fill = false;

    // HT_INFOF("parsing: %s", opt.c_str());
    cfg_name = parse_opt(opt); // << input need to be NAME=VALUE else false
    if(!cfg_name){
      name = m_cfg.position_name(-1);
      if(!name.empty())
        set_pos_parse(name, raw_opt);
      else if(!m_unregistered) 
        // if no pos -1  if(n!=0)ignore app-file, unregistered arg-positions
        HT_THROWF(Error::CONFIG_GET_ERROR, "unknown cfg  with %s", raw_opt.c_str());
    }
    opt.clear();
  }
  make_options();
}

void Parser::parse_line(String line){
  String name, value;
  size_t at, cmt;
  at = line.find_first_of("="); // is a cfg type
  if(at==std::string::npos) 
    return;
      
  name = line.substr(0, at);
  cmt = name.find_first_of("#"); // is comment in name
  if(cmt != std::string::npos) 
    return;
      
  name.erase(std::remove_if(name.begin(), name.end(),
                [](unsigned char x){return std::isspace(x);}),
             name.end());

  value = line.substr(at+1);
  cmt = value.find_first_of("#"); // remove followup comment
  if(cmt != std::string::npos)
    value = value.substr(0, cmt);

  at = value.find_first_of("\"");
  if(at != std::string::npos) {   // quoted value
    value = value.substr(at+1);
    at = value.find_first_of("\"");
    if(at != std::string::npos)
      value = value.substr(0, at);
  } else                 // remove spaces
    value.erase(std::remove_if(value.begin(), value.end(),
                  [](unsigned char x){return std::isspace(x);}),
                value.end());

  parse_opt(name+"="+value); // << input must be NAME=VALUE !

  //cfg_name = parse_opt(line);
  // if(!cfg_name)
  //  HT_WARNF("unknown cfg: %s", line.c_str());
  line.clear();
}

void Parser::set_pos_parse(const String name, String value){
  InsRet r = raw_opts.insert(Pair(name, Strings()));
  (*r.first).second.push_back(value);
}

bool Parser::parse_opt(String s){
  size_t at = s.find_first_of("=");
  if(at==std::string::npos) return false;

  String name = s.substr(0, at);
  // HT_INFOF("looking: %s", name.c_str());
  if(!m_cfg.has(name)&&!m_unregistered)
    return false;
    
  InsRet r = raw_opts.insert(Pair(name, Strings()));

  String value = s.substr(at+1);
  // HT_INFOF("value: %s", value.c_str());
  if(value.empty()) 
    return true;
  (*r.first).second.push_back(value);
  return true;
}
  
void Parser::print(std::ostream& os) {
  os << String("*** Raw Parsed Options:") << "\n";
  for (const auto &kv : raw_opts)
    os << kv.first << "=" << format_list(kv.second) << "\n";
}

void Parser::make_options(){
  for (const auto &kv : raw_opts){
    String tmp_name = String(kv.first);
    if(!m_cfg.has(tmp_name) && m_unregistered)
      add_opt(kv.first, str(), kv.second);  // unregistered cfg
    else
      add_opt(kv.first, m_cfg.get_default(kv.first), kv.second);
  }
  for (const auto &kv : m_cfg.get_map()) {
    if(raw_opts.count(kv.first) > 0 || kv.second.value->is_skippable())
    continue;
    // add default kv
    add_opt(kv.first, kv.second.value, {});
  }
}

void Parser::add_opt(const String name, Property::ValuePtr p, Strings raw_opt){
  Property::ValuePtr p_set = Property::make_new(p, raw_opt);
  if(raw_opt.empty())
    p_set->default_value();
  if(p->is_guarded())
    p_set->guarded(true);
  m_opts.insert(OptPair(name, p_set));
}
  
void Parser::print_options(std::ostream& os) {
  os << String("*** Parsed Options:") << "\n";
  for (const auto &kv : m_opts)
    os << kv.first << "=" << kv.second->str() << "\n";
}

std::ostream& operator<<(std::ostream& os,  Parser& prs) {
  prs.print(os);
  return os;
}



} // Config namespace
} // Hypertable namespace
