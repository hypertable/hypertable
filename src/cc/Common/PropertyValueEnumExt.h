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

#ifndef Common_PropertyEnumExt_h
#define Common_PropertyEnumExt_h

#include <string>
#include <atomic>
#include <functional>
#include <Common/String.h>


namespace Hypertable {

/** @addtogroup Common
 *  @{
 */

namespace Property {


class ValueEnumExtBase {
  public:

    virtual void set_value(int nv){}

    virtual int get() { return -1; }

    ValueEnumExtBase* operator =(int nv){
      set_value(nv);
      return *this;
    }

    bool operator==(ValueEnumExtBase a) { return get() == a.get(); }
    bool operator!=(ValueEnumExtBase a) { return get() != a.get(); }

    operator ValueEnumExtBase*() { return this;  }
    operator int()      { return get(); }
    operator String()   { return str(); }
    

    ValueEnumExtBase* operator =(ValueEnumExtBase other){
      set_from(other);
      return *this;
    }

    void set_from(ValueEnumExtBase &other){
      if((int)other != -1)
        set_value((int)other);
      if(!other.cb_set) 
        return;
      
      set_repr(other.get_call_repr());
      set_from_string(other.get_call_from_string());
    }

    ValueEnumExtBase& set_from_string(std::function<int(String)> cb) {
      call_from_string = cb;
      cb_set = true;
      return *this;
    }
    
    ValueEnumExtBase& set_repr(std::function<String(int)> cb) {
      call_repr = cb;
      cb_set = true;
      return *this;
    }

    int from_string(String opt);
    
    void set_default_calls();

    std::function<int(String)> get_call_from_string(){
      if(!cb_set) set_default_calls();
      return call_from_string;
    }

    std::function<String(int)> get_call_repr(){
      if(!cb_set) set_default_calls();
      return call_repr;
    }

    String str() {
      return get_call_repr()(get());
    }

    String to_str();

    virtual ~ValueEnumExtBase() {};

  private:
    bool cb_set = false; 
    std::function<int(String)> call_from_string;
    std::function<String(int)> call_repr;
};


class ValueEnumExt : public ValueEnumExtBase {
  public:

    ValueEnumExt(int nv = -1) { 
      set_value(nv);
    }

    void set_value(int nv) override {
      value = nv;
    }

    int get() override {
      return value; 
    }

    ~ValueEnumExt () {};

  private:
    int value;
};


class ValueGuardedEnumExt : public ValueEnumExtBase {
  public:

    ValueGuardedEnumExt(int nv = -1) {
      set_value(nv);
    }

    ValueGuardedEnumExt(ValueGuardedEnumExt& other) {
      set_from(other);
    }
    
    ValueGuardedEnumExt& operator=(ValueGuardedEnumExt& other) {
      set_from(other); 
      return *this; 
    }

    void set_value(int nv) override {
      if(nv == get()) 
        return;
        
      value.store(nv);
      if(on_chg_cb)
        on_chg_cb(get());
    }

    int get() override { 
      return value.load(); 
    }

    void set_cb_on_chg(std::function<void(int)> cb) {
      on_chg_cb = cb;
    }

    ~ValueGuardedEnumExt () {};

  private:
    std::atomic<int> value;
    std::function<void(int)> on_chg_cb;
};

} // namespace Property


typedef Property::ValueEnumExt EnumExt;

typedef Property::ValueGuardedEnumExt gEnumExt;
typedef Property::ValueGuardedEnumExt* gEnumExtPtr;


/** @} */

} // namespace Hypertable

#endif // Common_PropertyEnumExt_h
