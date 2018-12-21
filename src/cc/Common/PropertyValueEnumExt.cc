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

#include <Common/PropertyValueEnumExt.h>

#include <Common/Compat.h>
#include <Common/Logger.h>
#include <Common/Error.h>

namespace Hypertable {

/** @addtogroup Common
 *  @{
 */

namespace Property {


int ValueEnumExtBase::from_string(String opt) {
  int nv = get_call_from_string()(opt);
  if(nv > -1)
    set_value(nv);
  else {
    if(get() == -1)
      HT_THROWF(Error::CONFIG_GET_ERROR, 
                "Bad Value %s, no corresponding enum", opt.c_str());
    else
      HT_WARNF("Bad cfg Value %s, no corresponding enum", opt.c_str());
  }
  return get();
}

String ValueEnumExtBase::to_str() {
  return format("%s  # (%d)", get_call_repr()(get()).c_str(), get());
}

void ValueEnumExtBase::set_default_calls() {
  call_from_string = [](String opt){
      HT_THROWF(Error::CONFIG_GET_ERROR, "Bad Value %s, no from_string cb set", opt.c_str());
      return -1;
  };
  call_repr = [](int v){return "No repr cb defined!";};
}


} // namespace Property

/** @} */

} // namespace Hypertable
