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
 * A Property Value used with Properties 
 */


#include <Common/Compat.h>
#include <Common/Logger.h>
#include <Common/Error.h>

#include <Common/Property.h>

namespace Hypertable {

namespace Property {


const ValueType get_value_type(const std::type_info &v_type){
  if(v_type == typeid(double))
    return ValueType::DOUBLE;

  if(v_type == typeid(bool))
    return ValueType::BOOL;  
  if(v_type == typeid(gBool))
    return ValueType::G_BOOL;

  if(v_type == typeid(String))
    return ValueType::STRING;  
  if(v_type == typeid(uint16_t))
    return ValueType::UINT16_T;  

  if(v_type == typeid(int32_t))
    return ValueType::INT32_T; 
  if(v_type == typeid(gInt32t))
    return ValueType::G_INT32_T;

  if(v_type == typeid(int64_t))
    return ValueType::INT64_T;  

  if(v_type == typeid(Strings))
    return ValueType::STRINGS;  
  if(v_type == typeid(gStrings))
    return ValueType::G_STRINGS;

  if(v_type == typeid(Int64s))
    return ValueType::INT64S;  
  if(v_type == typeid(Doubles))
    return ValueType::DOUBLES;

  if(v_type == typeid(EnumExt))
    return ValueType::ENUMEXT;
  if(v_type == typeid(gEnumExt))
    return ValueType::G_ENUMEXT;
    
  return ValueType::UNKNOWN;
}

/**
 * Convertors & Validators from String
*/

double double_from_string(String s){
  char *last;
  double res = strtod(s.c_str(), &last);

  if (s.c_str() == last)
    HT_THROWF(Error::CONFIG_GET_ERROR, "Bad Value %s", s.c_str());

  switch (*last) {
    case 'k':
    case 'K': res *= 1000LL;         break;
    case 'm':
    case 'M': res *= 1000000LL;      break;
    case 'g':
    case 'G': res *= 1000000000LL;   break;
    case '\0':                          break;
    default: 
      HT_THROWF(Error::CONFIG_GET_ERROR, 
                "Bad Value %s unknown suffix %s", s.c_str(), last);
  }
  return res;
}

int64_t int64_t_from_string(String s){
  char *last;
  int64_t res = strtoll(s.c_str(), &last, 0);

  if (s.c_str() == last)
    HT_THROWF(Error::CONFIG_GET_ERROR, "Bad Value %s", s.c_str());

  if (res > INT64_MAX || res < INT64_MIN) 
    HT_THROWF(Error::CONFIG_GET_ERROR, 
              "Bad Value %s, number out of range of 64-bit integer", s.c_str());
  
  switch (*last) {
    case 'k':
    case 'K': res *= 1000LL;         break;
    case 'm':
    case 'M': res *= 1000000LL;      break;
    case 'g':
    case 'G': res *= 1000000000LL;   break;
    case '\0':                          break;
    default: 
      HT_THROWF(Error::CONFIG_GET_ERROR, 
                "Bad Value %s unknown suffix %s", s.c_str(), last);
  }
  return res;
}

uint16_t uint16_t_from_string(String s){
  int64_t res = int64_t_from_string(s);

  if (res > UINT16_MAX || res < -UINT16_MAX) 
    HT_THROWF(Error::CONFIG_GET_ERROR, 
              "Bad Value %s, number out of range of 16-bit integer", s.c_str());
  return (uint16_t)res;
}

int32_t int32_t_from_string(String s){
  int64_t res = int64_t_from_string(s);

  if (res > INT32_MAX || res < INT32_MIN) 
    HT_THROWF(Error::CONFIG_GET_ERROR, 
              "Bad Value %s, number out of range of 32-bit integer", s.c_str());
  return (int32_t)res;
}


template <>
ValueDef<EnumExt>::ValueDef(ValueType typ, Strings values, EnumExt defaulted) {
  set_type(typ);
  get_ptr()->set_from(defaulted);  // assign call functions if set
  if(!values.empty())
    from_strings(values);
}
template <>
ValueDef<gEnumExt>::ValueDef(ValueType typ, Strings values, gEnumExt defaulted) {
  set_type(typ);
  get_ptr()->set_from(defaulted);  // assign call functions if set
  if(!values.empty())
    from_strings(values);
}

template <>
void ValueDef<bool>::from_strings(Strings values) {
  bool res;
  String str = values.back();
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  res = (str.compare("1")==0)||(str.compare("true")==0)||(str.compare("yes")==0);
  set_value(res);
}
template <>
void ValueDef<gBool>::from_strings(Strings values) {
  bool res;
  String str = values.back();
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  res = (str.compare("1")==0)||(str.compare("true")==0)||(str.compare("yes")==0);
  set_value(res);
}
template <>
void ValueDef<String>::from_strings(Strings values){
  set_value(values.back());
}
template <>
void ValueDef<double>::from_strings(Strings values) {
  set_value(double_from_string(values.back()));
}
template <>
void ValueDef<uint16_t>::from_strings(Strings values) {
  set_value(uint16_t_from_string(values.back()));
}
template <>
void ValueDef<int32_t>::from_strings(Strings values) {
  set_value(int32_t_from_string(values.back()));
}
template <>
void ValueDef<gInt32t>::from_strings(Strings values) {
  set_value(int32_t_from_string(values.back()));
}
template <>
void ValueDef<int64_t>::from_strings(Strings values) {
  set_value(int64_t_from_string(values.back()));
}
template <>
void ValueDef<Doubles>::from_strings(Strings values){
  Doubles value;
  for(String s: values)
    value.push_back(double_from_string(s));
  set_value(value);
}
template <>
void ValueDef<Int64s>::from_strings(Strings values){
  Int64s value;
  for(String s: values)
    value.push_back(int64_t_from_string(s));
  set_value(value);
}
template <>
void ValueDef<Strings>::from_strings(Strings values){
  set_value(values);
}
template <>
void ValueDef<gStrings>::from_strings(Strings values){
  set_value(values);
}
template <>
void ValueDef<EnumExt>::from_strings(Strings values){
  get_ptr()->from_string(values.back());
}
template <>
void ValueDef<gEnumExt>::from_strings(Strings values){
  get_ptr()->from_string(values.back());
}


template <>
String ValueDef<bool>::str(){
  return get_value() ? "true" : "false";;
}
template <>
String ValueDef<gBool>::str(){
  return (bool)get_value() ? "true" : "false";;
}
template <>
String ValueDef<String>::str(){
  return get_value();
}
template <>
String ValueDef<double>::str(){
  return format("%g", get_value());
}
template <>
String ValueDef<uint16_t>::str(){
  return format("%u", (unsigned)get_value());
}
template <>
String ValueDef<int32_t>::str(){
  return format("%d", get_value());
}
template <>
String ValueDef<gInt32t>::str(){
  return format("%d", (int32_t)get_value());
}
template <>
String ValueDef<int64_t>::str(){
  return format("%ld", get_value());
}
template <>
String ValueDef<Doubles>::str(){
  return format_list(get_value());
}
template <>
String ValueDef<Int64s>::str(){
  return format_list(get_value());
}
template <>
String ValueDef<Strings>::str(){
  return format_list(get_value());
}
template <>
String ValueDef<gStrings>::str(){
  return format_list((Strings)get_value());
}
template <>
String ValueDef<EnumExt>::str(){
  return v.to_str();
}
template <>
String ValueDef<gEnumExt>::str(){
  return v.to_str();
}



void Value::set_value_from(ValuePtr from){
  switch(get_type()){
    case ValueType::STRING:
      return set_value(from->get<String>());

    case ValueType::BOOL: 
      return set_value(from->get<bool>());
    case ValueType::G_BOOL: 
      return set_value(from->get<gBool>());

    case ValueType::DOUBLE:
      return set_value(from->get<double>());
    case ValueType::UINT16_T:
      return set_value(from->get<uint16_t>());

    case ValueType::INT32_T:
      return set_value(from->get<int32_t>());
    case ValueType::G_INT32_T: 
      return set_value(from->get<gInt32t>());

    case ValueType::INT64_T:
      return set_value(from->get<int64_t>());

    case ValueType::STRINGS:
      return set_value(from->get<Strings>());
    case ValueType::G_STRINGS:
      return set_value(from->get<gStrings>());

    case ValueType::INT64S:
      return set_value(from->get<Int64s>());
    case ValueType::DOUBLES:
      return set_value(from->get<Doubles>());

    case ValueType::ENUMEXT:
      return set_value(from->get<EnumExt>());
    case ValueType::G_ENUMEXT:
      return set_value(from->get<gEnumExt>());

    case ValueType::ENUM:
    default:
      HT_THROWF(Error::CONFIG_GET_ERROR, "Bad Type %s", str().c_str());
  }
}

String Value::str(){
  if (type_ptr == nullptr)
    return "nullptr";
      
  switch(get_type()){
    case ValueType::STRING:
      return ((ValueDef<String>*)type_ptr)->str();

    case ValueType::BOOL: 
      return ((ValueDef<bool>*)type_ptr)->str();
    case ValueType::G_BOOL: 
      return ((ValueDef<gBool>*)type_ptr)->str();

    case ValueType::DOUBLE:
      return ((ValueDef<double>*)type_ptr)->str();

    case ValueType::UINT16_T:
      return ((ValueDef<uint16_t>*)type_ptr)->str();

    case ValueType::INT32_T:
      return ((ValueDef<int32_t>*)type_ptr)->str();
    case ValueType::G_INT32_T: 
      return ((ValueDef<gInt32t>*)type_ptr)->str();

    case ValueType::INT64_T:
      return ((ValueDef<int64_t>*)type_ptr)->str();

    case ValueType::STRINGS:
      return ((ValueDef<Strings>*)type_ptr)->str();
    case ValueType::G_STRINGS: 
      return ((ValueDef<gStrings>*)type_ptr)->str();

    case ValueType::INT64S:
      return ((ValueDef<Int64s>*)type_ptr)->str();
    case ValueType::DOUBLES:
      return ((ValueDef<Doubles>*)type_ptr)->str();

    case ValueType::ENUMEXT:
      return ((ValueDef<EnumExt>*)type_ptr)->str();
    case ValueType::G_ENUMEXT:
      return ((ValueDef<gEnumExt>*)type_ptr)->str();

    case ValueType::ENUM:
      return "An ENUM TYPE";
    default:
      return "invalid option type";
  }
}

ValuePtr make_new(ValuePtr p, Strings values){
  ValueType typ = p->get_type();
  switch(typ){

    case ValueType::STRING:
      return new Value(
        (TypeDef*)new ValueDef<String>(typ, values, p->get<String>()));

    case ValueType::BOOL: 
      return new Value(
        (TypeDef*)new ValueDef<bool>(typ, values, p->get<bool>()));
    case ValueType::G_BOOL: 
      return new Value(
        (TypeDef*)new ValueDef<gBool>(typ, values, p->get<gBool>()));

    case ValueType::DOUBLE:
      return new Value(
        (TypeDef*)new ValueDef<double>(typ, values, p->get<double>()));

    case ValueType::UINT16_T:
      return new Value(
        (TypeDef*)new ValueDef<uint16_t>(typ, values, p->get<uint16_t>()));

    case ValueType::INT32_T:
      return new Value(
        (TypeDef*)new ValueDef<int32_t>(typ, values, p->get<int32_t>()));
    case ValueType::G_INT32_T: 
      return new Value(
        (TypeDef*)new ValueDef<gInt32t>(typ, values, p->get<gInt32t>()));

    case ValueType::INT64_T:
      return new Value(
        (TypeDef*)new ValueDef<int64_t>(typ, values, p->get<int64_t>()));

    case ValueType::STRINGS:
      return new Value(
        (TypeDef*)new ValueDef<Strings>(typ, values, p->get<Strings>()));
    case ValueType::G_STRINGS:
      return new Value(
        (TypeDef*)new ValueDef<gStrings>(typ, values, p->get<gStrings>()));

    case ValueType::INT64S:
      return new Value(
        (TypeDef*)new ValueDef<Int64s>(typ, values, p->get<Int64s>()));

    case ValueType::DOUBLES:
      return new Value(
        (TypeDef*)new ValueDef<Doubles>(typ, values, p->get<Doubles>()));

    case ValueType::ENUMEXT:
      return new Value(
        (TypeDef*)new ValueDef<EnumExt>(typ, values, p->get<EnumExt>()));
    case ValueType::G_ENUMEXT:
      return new Value(
        (TypeDef*)new ValueDef<gEnumExt>(typ, values, p->get<gEnumExt>()));
      
    case ValueType::ENUM:
    default:
      HT_THROWF(Error::CONFIG_GET_ERROR, 
                "Bad Type for values %s", format_list(values).c_str());
  }
}


} // namespace Property

} // namespace Hypertable
