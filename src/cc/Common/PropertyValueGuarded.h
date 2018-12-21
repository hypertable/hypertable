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
 * A Property Value Guarded used with Properties 
 */

#ifndef Common_PropertyValueGuarded_h
#define Common_PropertyValueGuarded_h

#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <functional>

#include <Common/Compat.h>


namespace Hypertable {

/** @addtogroup Common
 *  @{
 */

typedef std::vector<String> Strings;
typedef std::vector<int64_t> Int64s;
typedef std::vector<double> Doubles;


namespace Property {

template <class T>
class ValueGuardedAtomic {
  
  public:

    ValueGuardedAtomic () noexcept {}
 
    ValueGuardedAtomic (T nv) noexcept {
      store(nv);
    }
 
    ValueGuardedAtomic (ValueGuardedAtomic &other) noexcept {
      store(other.get());
    }
 
    void store(T nv){
      if(nv == vg.load()) 
        return;
      vg.store(nv);
      if(on_chg_cb)
        on_chg_cb();
    }

    ~ValueGuardedAtomic () noexcept {};
    
    operator ValueGuardedAtomic*() { 
      return this;    
    }
    
    ValueGuardedAtomic* operator =(T nv) {
      store(nv);
      return *this;
    }

    ValueGuardedAtomic* operator =(ValueGuardedAtomic &other) {
      store(other.get());
      return *this;
    }
    
    operator T() {
      return vg.load(); 
    }

    T get() {
      return vg.load(); 
    }

    operator std::atomic<T>*() {
      return &vg; 
    }

    void set_cb_on_chg(std::function<void()> cb) {
      on_chg_cb = cb;
    }

  private:
    std::atomic<T> vg;
    std::function<void()> on_chg_cb;
};

}

typedef Property::ValueGuardedAtomic<bool>      gBool;
typedef Property::ValueGuardedAtomic<int32_t>   gInt32t;
typedef gBool*    gBoolPtr;
typedef gInt32t*  gInt32tPtr;


namespace Property {

template <class T>
class ValueGuardedVector {

  private:

    std::mutex mutex;	
    T v;
    using T_of = typename std::decay<decltype(*v.begin())>::type;

  public:

    ValueGuardedVector () noexcept {}
 
    ValueGuardedVector (T nv) noexcept {
      set(nv);
    }
 
    ValueGuardedVector (ValueGuardedVector &other) noexcept {
      set(other.get());
    }
 
    ~ValueGuardedVector () noexcept {};
    
    operator ValueGuardedVector*()   { 
      return this;    
    }
    
    ValueGuardedVector* operator =(T nv){
      set(nv);
      return *this;
    }

    ValueGuardedVector* operator =(ValueGuardedVector &other){
      set(other.get());
      return *this;
    } 
    
    void set(T nv)  {
      std::lock_guard<std::mutex> lock(mutex);	
      v.swap(nv);
    }
    
    operator T(){
      return get(); 
    }

    T get(){
      std::lock_guard<std::mutex> lock(mutex);	
      return v;      
    }

    size_t size()  {
      std::lock_guard<std::mutex> lock(mutex);	
      return v.size();
    }

    T_of get_item(size_t n){
      std::lock_guard<std::mutex> lock(mutex);	
      return v[n];
    }

};

} // namespace Property


typedef Property::ValueGuardedVector<Strings> gStrings;
typedef gStrings* gStringsPtr;


/** @} */

} // namespace Hypertable

#endif // Common_PropertyValueGuarded_h
