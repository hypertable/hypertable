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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#ifndef HYPERTABLE_SERIALIZEDCELLSREADER_H
#define HYPERTABLE_SERIALIZEDCELLSREADER_H

#include "Hypertable/Lib/KeySpec.h"
#include "Hypertable/Lib/Key.h"
#include "Hypertable/Lib/Cell.h"

#include "SerializedCellsFlag.h"

namespace Hypertable {

  class SerializedCellsReader {
  public:

    SerializedCellsReader(void *buf, uint32_t len) {
      init((uint8_t *)buf, len);
    }

    SerializedCellsReader(const char *buf, uint32_t len) {
      init((uint8_t *)buf, len);
    }

    SerializedCellsReader(std::string buf, uint32_t len) {
      init((uint8_t *)buf.c_str(), len);
    }
	
    bool next();

    void get(KeySpec &key) {
      key.row = m_row;
      key.row_len = strlen(m_row);
      key.column_family = m_column_family;
      key.column_qualifier = m_column_qualifier;
      key.column_qualifier_len = m_column_qualifier ? strlen(m_column_qualifier) : 0;
      key.timestamp = m_timestamp;
      key.revision = m_revision;
    }

    void get(Cell &cell) {
      cell.row_key = m_row;
      cell.column_family = m_column_family;
      cell.column_qualifier = m_column_qualifier;
      cell.timestamp = m_timestamp;
      cell.revision = m_revision;
      cell.value = (uint8_t*)m_value;
      cell.value_len = m_value_len;
      cell.flag = m_cell_flag;
    }

    Cell get_cell() {
      Cell cell;
      get(cell);
      return cell;
    }

    const char *row() { return m_row; }
    const char *column_family() { return m_column_family; }
    const char *column_qualifier() { return m_column_qualifier; }
    const void *value() { return m_value; }
    const char *value_str() { return (const char *)m_value; }
    uint32_t value_len() { return m_value_len; }
    int64_t timestamp() { return m_timestamp; }
    int8_t cell_flag() { return m_cell_flag; }

    bool flush() { return (m_flag & SerializedCellsFlag::FLUSH) > 0; }
    bool eos() { return (m_flag & SerializedCellsFlag::EOS) > 0; }

  private:
    void init(uint8_t *buf, uint32_t len) {
		//std::string output(len, 0);
		//for (size_t i = 0; i < len; ++i)
		//	output[i] = static_cast<char>(buf[i]);
		//HT_THROW(Error::BAD_FORMAT,
		//	"SerializedCellsReader: input: buf: " + output + "len:" + std::to_string(len));

      m_base = m_ptr = (uint8_t *)buf;
      m_end = m_base + len;

      size_t remaining = m_end - m_ptr;
      int32_t version = Serialization::decode_i32(&m_ptr, &remaining);
      if (version != SerializedCellsVersion::SCVERSION)
        HT_THROW(Error::SERIALIZATION_VERSION_MISMATCH, "");
    }

    const uint8_t *m_base {};
    const uint8_t *m_ptr {};
    const uint8_t *m_end {};
    const char *m_row {};
    const char *m_column_family {};
    const char *m_column_qualifier {};
    int64_t m_timestamp {TIMESTAMP_NULL};
    int64_t m_revision {TIMESTAMP_NULL};
    const void *m_value {};
    uint32_t m_value_len {};
    uint8_t m_cell_flag {FLAG_INSERT};
    uint8_t m_flag {};
    bool m_eob {};
    const char *m_previous_row {};
  };

}

#endif // HYPERTABLE_SERIALIZEDCELLSREADER_H
