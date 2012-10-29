/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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
#ifndef HYPERTABLE_STRINGEXT_H
#define HYPERTABLE_STRINGEXT_H

#include <cstdio>
#include <stdexcept>
#include <set>
#include <map>

#include "Common/String.h"
#include "HashMap.h"


typedef std::set<std::string> StringSet;


/** STL Strict Weak Ordering for comparing c-style strings. */
struct LtCstr {
  bool operator()(const char* s1, const char* s2) const {
    return strcmp(s1, s2) < 0;
  }
};

typedef std::set<const char *, LtCstr>  CstrSet;

typedef std::map<const char *, int32_t, LtCstr>  CstrToInt32Map;

namespace std {
inline std::string operator+(const std::string& s1, short sval) {
  char cbuf[8];
  sprintf(cbuf, "%d", sval);
  return s1 + cbuf;
}

inline std::string operator+(const std::string& s1, int ival) {
  char cbuf[16];
  sprintf(cbuf, "%d", ival);
  return s1 + cbuf;
}

inline std::string operator+(const std::string& s1, uint32_t ival) {
  char cbuf[16];
  sprintf(cbuf, "%d", ival);
  return s1 + cbuf;
}

inline std::string operator+(const std::string& s1, int64_t llval) {
  char cbuf[32];
  sprintf(cbuf, "%lld", (long long int)llval);
  return s1 + cbuf;
}

inline std::string operator+(const std::string& s1, uint64_t llval) {
  char cbuf[32];
  sprintf(cbuf, "%llu", (long long unsigned int)llval);
  return s1 + cbuf;
}
}

#endif // HYPERTABLE_STRINGEXT_H
