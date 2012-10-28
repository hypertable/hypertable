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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#ifndef HYPERTABLE_CHARSTR_HASHMAP_H
#define HYPERTABLE_CHARSTR_HASHMAP_H

#include "HashMap.h"
#include "BlobHashTraits.h"

namespace Hypertable {

/**
 * A hash set for storing and lookup blob efficiently
 */
template <class TraitsT = BlobHashTraits<> >
class BlobHashSet : public hash_set<Blob, typename TraitsT::hasher,
                                    typename TraitsT::key_equal> {
private:
  typedef hash_set<Blob, typename TraitsT::hasher,
                   typename TraitsT::key_equal> Base;
public:
  typedef typename Base::iterator iterator;
  typedef typename Base::key_type key_type;
  typedef typename Base::value_type value_type;
  typedef typename TraitsT::key_allocator key_allocator;
  typedef std::pair<iterator, bool> InsRet;

  BlobHashSet() { }
  BlobHashSet(size_t n_buckets) : Base(n_buckets) { }
  ~BlobHashSet() { }

  // hides all insert methods in base class
  InsRet insert(const void *buf, size_t len) {
    return insert_blob(len, Base::insert(value_type(buf, len)));
  }

  InsRet insert(const String &s) {
    return insert_blob(s.size(), Base::insert(value_type(s.data(), s.size())));
  }

  InsRet insert_noresize(const void *buf, size_t len) {
    return insert_blob(len, Base::insert_noresize(value_type(buf, len)));
  }

  InsRet insert(const Blob &blob) {
    return insert_blob(blob.size, Base::insert(blob));
  }

  InsRet insert_noresize(const Blob &blob) {
    return insert_blob(blob.size, Base::insert_noresize(blob));
  }

  // hides all find methods in base class
  iterator find(const Blob &blob) const { return Base::find(blob); }

  iterator find(const String &s) const {
    return Base::find(Blob(s.data(), s.size()));
  }

  key_allocator &key_alloc() { return m_alloc; }

  void clear() { Base::clear(); }

private:
  key_allocator m_alloc;

private:
  InsRet insert_blob(size_t len, InsRet rv) {
    if (rv.second)
      const_cast<const void *&>(rv.first->start) =
          m_alloc.dup(rv.first->start, len);

    return rv;
  }
};

} // namespace Hypertable

#endif //! HYPERTABLE_CHARSTR_HASHMAP_H
