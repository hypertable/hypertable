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

#ifndef Common_Property_h
#define Common_Property_h

#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <functional>

#include <Common/Compat.h>
#include <Common/PropertyValueEnumExt.h>
#include <Common/PropertyValueGuarded.h>

#include <Common/Logger.h>
#include <Common/Error.h>

namespace Hypertable {

/** @addtogroup Common
 *  @{
 */

const uint64_t K = 1000;
const uint64_t KiB = 1024;
const uint64_t M = K * 1000;
const uint64_t MiB = KiB * 1024;
const uint64_t G = M * 1000;
const uint64_t GiB = MiB * 1024;



namespace Property {

enum ValueType {
  UNKNOWN,
  DOUBLE,
  BOOL,
  STRING,
  UINT16_T,
  INT32_T,
  INT64_T,
  STRINGS,
  INT64S,
  DOUBLES,
  ENUMEXT,
  ENUM,
  
  G_BOOL,
  G_INT32_T,
  G_STRINGS,
  G_ENUMEXT
};

const ValueType get_value_type(const std::type_info &v_type);

/**
 * Convertors & Validators from String
*/
double double_from_string(String s);
int64_t int64_t_from_string(String s);
uint16_t uint16_t_from_string(String s);
int32_t int32_t_from_string(String s);

/**
* Property::TypeDef
* a Helper class for casting ValueDef<T> on a single type Pointer
* with a hold for corresponding ValueType enum
 */
class TypeDef {
  public:
    ValueType get_type()  { return typ; }
    void set_type(const ValueType t){ 
      if(typ != t)
        typ = t;  
    }
    virtual ~TypeDef() {}
  private:
    ValueType typ;
};
 
/**
* Property::ValueDef
* the Class handle the specific requirments to the 
* value type
 */
template <class T>
class  ValueDef : public TypeDef {
  public:
    static constexpr bool is_enum() {  
      return std::is_enum<T>::value; 
    }

    ValueDef(ValueType typ, Strings values, T defaulted) {
      set_type(typ);
      if(!values.empty())
        from_strings(values);
      else
        set_value(defaulted);
    }

    ValueDef(T nv) { 
      if(is_enum())
        set_type(ValueType::ENUM);
      else
        set_type(get_value_type(typeid(T)));
      set_value(nv);  
    }

    void set_value(T nv) {
      v = nv; 
    }

    void from_strings(Strings values) {};

    T get_value() {
      return v;       
    }

    T* get_ptr() { 
      return &v;      
    }

    String str(){
      return "invalid option type";
    }

    std::ostream& operator<<(std::ostream& ostream) {
      return ostream << str();
    }

    operator TypeDef*() { 
      return this;
    }

    // ValueDef<T> operator *()   { return this;    }
    virtual ~ValueDef()   {}

  private:
    T v;
};

template <>
ValueDef<EnumExt>::ValueDef(ValueType typ, Strings values, EnumExt defaulted);
template <>
ValueDef<gEnumExt>::ValueDef(ValueType typ, Strings values, gEnumExt defaulted);


/* set_value from_strings */
template <>
void ValueDef<bool>::from_strings(Strings values);
template <>
void ValueDef<gBool>::from_strings(Strings values);
template <>
void ValueDef<String>::from_strings(Strings values);
template <>
void ValueDef<double>::from_strings(Strings values);
template <>
void ValueDef<uint16_t>::from_strings(Strings values);
template <>
void ValueDef<int32_t>::from_strings(Strings values);
template <>
void ValueDef<gInt32t>::from_strings(Strings values);
template <>
void ValueDef<int64_t>::from_strings(Strings values);
template <>
void ValueDef<Doubles>::from_strings(Strings values);
template <>
void ValueDef<Int64s>::from_strings(Strings values);
template <>
void ValueDef<Strings>::from_strings(Strings values);
template <>
void ValueDef<gStrings>::from_strings(Strings values);
template <>
void ValueDef<EnumExt>::from_strings(Strings values);
template <>
void ValueDef<gEnumExt>::from_strings(Strings values);

/* return string representation */
template <>
String ValueDef<bool>::str();
template <>
String ValueDef<gBool>::str();
template <>
String ValueDef<String>::str();
template <>
String ValueDef<double>::str();
template <>
String ValueDef<uint16_t>::str();
template <>
String ValueDef<int32_t>::str();
template <>
String ValueDef<gInt32t>::str();
template <>
String ValueDef<int64_t>::str();
template <>
String ValueDef<Doubles>::str();
template <>
String ValueDef<Int64s>::str();
template <>
String ValueDef<Strings>::str();
template <>
String ValueDef<gStrings>::str();
template <>
String ValueDef<EnumExt>::str();
template <>
String ValueDef<gEnumExt>::str();


    
/**
* Property::Value 
* holds:
*   * the pointer to TypeDef and short forms to ValueDef operations
*   * the whether the value is a default value or skippable
 */

class Value;
typedef Value* ValuePtr;

class Value {
  public:

    template<typename T>
    Value(T v, bool skippable=false, bool guarded=false) {
      m_skippable = skippable;
      m_guarded = guarded;
      set_value(v);
    }
    
    /* init from (TypeDef*)ValueDef<T> */
    Value(TypeDef* v) {
      type_ptr = v;
    }
    
    // update/set the new value to the ValueDef<T>
    template<typename T>
    void set_value(T v) {
      if(type_ptr == nullptr){
        type_ptr = new ValueDef<T>(v);
        return;
      } 
      ((ValueDef<T>*)type_ptr)->set_value(v);
    }

    /* set value from from other ValuePtr */
    void set_value_from(ValuePtr from);

    template<typename T>
    T get() {
      if (type_ptr == nullptr)
        HT_THROWF(Error::CONFIG_GET_ERROR, 
                  "T get(): type=%s (UNKNOWN VALUE TYPE)", typeid(T).name());;

      return ((ValueDef<T>*)type_ptr)->get_value();
    }

    template<typename T>
    T* get_ptr() {
      return ((ValueDef<T>*)type_ptr)->get_ptr();
    }

    TypeDef* get_type_ptr() {
      return type_ptr;
    }

    ValueType get_type() {
      return type_ptr->get_type();
    }
    
    /* represent value in string */
    String str();
    
    /* a Default Value */
    ValuePtr default_value(bool defaulted=true){
      m_defaulted = defaulted;
      return *this;
    }

    bool is_default() {
      return m_defaulted;
    }

    /* a Skippable property (no default value) */
    bool is_skippable() {
      return m_skippable;
    }
    
    /* a Zero Token Type */
    ValuePtr zero_token(){
      m_no_token = true;
      return *this;
    }
    bool is_zero_token(){
      return m_no_token;
    }

    /* a Guarded Type */
    bool is_guarded(){
      return m_guarded;
    }
    void guarded(bool guarded){
      m_guarded = guarded;
    }

    operator Value*() { 
      return this;
    }

    virtual ~Value() {}

  private:

    TypeDef* type_ptr = nullptr; //

    std::atomic<bool> m_defaulted = false;
    std::atomic<bool> m_no_token = false;
    std::atomic<bool> m_skippable = false;
    std::atomic<bool> m_guarded = false;

};


ValuePtr make_new(ValuePtr p, Strings values = Strings());



} // namespace Property

/** @} */

} // namespace Hypertable

#endif // Common_Property_h
