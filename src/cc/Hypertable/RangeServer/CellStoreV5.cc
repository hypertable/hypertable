/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#include "Common/Compat.h"
#include <cassert>

#include <boost/algorithm/string.hpp>
#include <boost/scoped_array.hpp>

#include "Common/Config.h"
#include "Common/Error.h"
#include "Common/Logger.h"
#include "Common/System.h"
#include "Common/StringCompressorPrefix.h"
#include "Common/StringDecompressorPrefix.h"
#include "Common/Time.h"

#include "AsyncComm/Protocol.h"

#include "Hypertable/Lib/BlockHeaderCellStore.h"
#include "Hypertable/Lib/CompressorFactory.h"
#include "Hypertable/Lib/Key.h"
#include "Hypertable/Lib/Schema.h"

#include "CellStoreV5.h"
#include "CellStoreInfo.h"
#include "CellStoreTrailerV5.h"
#include "CellStoreScanner.h"

#include "FileBlockCache.h"
#include "Global.h"
#include "Config.h"
#include "KeyCompressorPrefix.h"
#include "KeyDecompressorPrefix.h"

using namespace std;
using namespace Hypertable;

namespace {
  const uint32_t MAX_APPENDS_OUTSTANDING = 3;
  const uint16_t BLOCK_HEADER_FORMAT = 0;
}


CellStoreV5::CellStoreV5(Filesystem *filesys)
  : m_filesys(filesys) {
  m_file_id = FileBlockCache::get_next_file_id();
  assert(sizeof(float) == 4);
}

CellStoreV5::CellStoreV5(Filesystem *filesys, SchemaPtr &schema)
  : m_filesys(filesys), m_schema(schema) {
  m_file_id = FileBlockCache::get_next_file_id();
  assert(sizeof(float) == 4);
}

CellStoreV5::~CellStoreV5() {
  try {
    delete m_compressor;
    delete m_bloom_filter;
    delete m_bloom_filter_items;
    if (m_fd != -1)
      m_filesys->close(m_fd);
    delete [] m_column_ttl;
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
  }

  Global::memory_tracker->subtract( sizeof(CellStoreV5) + sizeof(CellStoreInfo) + m_index_stats.bloom_filter_memory + m_index_stats.block_index_memory );

}


BlockCompressionCodec *CellStoreV5::create_block_compression_codec() {
  return CompressorFactory::create_block_codec(
      (BlockCompressionCodec::Type)m_trailer.compression_type);
}

KeyDecompressor *CellStoreV5::create_key_decompressor() {
  return new KeyDecompressorPrefix();
}

void CellStoreV5::split_row_estimate_data(SplitRowDataMapT &split_row_data) {
  if (m_index_stats.block_index_memory == 0)
    load_block_index();
  if (m_trailer.index_entries == 0) {
    HT_WARNF("%s has 0 index entries", m_filename.c_str());
    return;
  }
  int32_t keys_per_block = m_trailer.total_entries / m_trailer.index_entries;
  if (m_64bit_index)
    m_index_map64.unique_row_count_estimate(split_row_data, keys_per_block);
  else
    m_index_map32.unique_row_count_estimate(split_row_data, keys_per_block);
}

CellListScannerPtr CellStoreV5::create_scanner(ScanContext *scan_ctx) {
  bool need_index =  m_restricted_range || scan_ctx->restricted_range || scan_ctx->use_index;

  if (need_index) {
    m_index_stats.block_index_access_counter = ++Global::access_counter;
    if (m_index_stats.block_index_memory == 0)
      load_block_index();
  }

  if (m_64bit_index)
    return make_shared<CellStoreScanner<CellStoreBlockIndexArray<int64_t>>>(shared_from_this(), scan_ctx, need_index ? &m_index_map64 : 0);
  return make_shared<CellStoreScanner<CellStoreBlockIndexArray<uint32_t>>>(shared_from_this(), scan_ctx, need_index ? &m_index_map32 : 0);
}

namespace {
  int get_replication(PropertiesPtr &props, const TableIdentifier *table_id) {

    int32_t replication = props->get_i32("replication", int32_t(-1));

    if (replication == -1 && table_id) {
      if (table_id->is_user()) {
	if (Config::has("Hypertable.RangeServer.Data.DefaultReplication"))
	  replication = Config::get_i32("Hypertable.RangeServer.Data.DefaultReplication");
      }
      else if (Config::has("Hypertable.Metadata.Replication"))
	replication = Config::get_i32("Hypertable.Metadata.Replication");
    }

    return replication;
  }
}

void
CellStoreV5::create(const char *fname, size_t max_entries,
                    PropertiesPtr &props, const TableIdentifier *table_id) {
  int64_t blocksize = props->get("blocksize", 0);
  String compressor = props->get("compressor", String());

  m_key_compressor = make_shared<KeyCompressorPrefix>();

  assert(Config::properties); // requires Config::init* first
  int32_t replication = get_replication(props, table_id);

  if (blocksize == 0)
    blocksize = Config::get_i32("Hypertable.RangeServer.CellStore"
                                ".DefaultBlockSize");
  if (compressor.empty())
    compressor = Config::get_str("Hypertable.RangeServer.CellStore"
                                 ".DefaultCompressor");
  if (!props->has("bloom-filter-mode")) {
    // probably not called from AccessGroup
    AccessGroupOptions::parse_bloom_filter(Config::get_str("Hypertable.RangeServer"
        ".CellStore.DefaultBloomFilter"), props);
  }

  m_buffer.reserve(blocksize*4);

  m_max_entries = max_entries;

  m_fd = -1;
  m_offset = 0;

  m_index_builder.fixed_buf().reserve(4*4096);
  m_index_builder.variable_buf().reserve(1024*1024);

  m_uncompressed_data = 0.0;
  m_compressed_data = 0.0;

  m_trailer.clear();
  m_trailer.blocksize = blocksize;
  m_uncompressed_blocksize = blocksize;

  // set up the "column_ttl" vector
  HT_ASSERT(m_schema);
  ColumnFamilySpecs &column_family_specs = m_schema->get_column_families();
  for (size_t i=0; i<column_family_specs.size(); i++) {
    if (column_family_specs[i]->get_option_ttl()) {
      if (m_column_ttl == 0) {
        m_column_ttl = new int64_t[256];
        memset(m_column_ttl, 0, 256*8);
      }
      m_column_ttl[ column_family_specs[i]->get_id() ] = column_family_specs[i]->get_option_ttl() * 1000000000LL;
    }
  }

  m_filename = fname;

  m_start_row = "";
  m_end_row = Key::END_ROW_MARKER;

  m_trailer.compression_type = CompressorFactory::parse_block_codec_spec(
      compressor, m_compressor_args);

  m_compressor = CompressorFactory::create_block_codec(
      (BlockCompressionCodec::Type)m_trailer.compression_type,
      m_compressor_args);

  uint32_t oflags = Filesystem::OPEN_FLAG_DIRECTIO|Filesystem::OPEN_FLAG_OVERWRITE;
  m_fd = m_filesys->create(m_filename, oflags, -1, replication, -1);

  m_bloom_filter_mode = props->get<BloomFilterMode>("bloom-filter-mode");
  m_max_approx_items = props->get_i32("max-approx-items");

  if (m_bloom_filter_mode != BLOOM_FILTER_DISABLED) {
    bool has_num_hashes = props->has("num-hashes");
    bool has_bits_per_item = props->has("bits-per-item");

    if (has_num_hashes || has_bits_per_item) {
      if (!(has_num_hashes && has_bits_per_item)) {
        HT_WARN("Bloom filter option --bits-per-item must be used with "
                "--num-hashes, defaulting to false probability of 0.01");
        m_filter_false_positive_prob = 0.1;
      }
      else {
        m_trailer.bloom_filter_hash_count = props->get_i32("num-hashes");
        m_bloom_bits_per_item = props->get_f64("bits-per-item");
      }
    }
    else
      m_filter_false_positive_prob = props->get_f64("false-positive");
    m_bloom_filter_items = new BloomFilterItems(); // aproximator items
  }
  HT_DEBUG_OUT <<"bloom-filter-mode="<< m_bloom_filter_mode
      <<" max-approx-items="<< m_max_approx_items <<" false-positive="
      << m_filter_false_positive_prob << HT_END;
}


void CellStoreV5::create_bloom_filter(bool is_approx) {
  assert(!m_bloom_filter && m_bloom_filter_items);

  HT_DEBUG_OUT << "Creating new BloomFilter for CellStore '"
    << m_filename <<"' for "<< (is_approx ? "estimated " : "")
    << m_trailer.filter_items_estimate << " items"<< HT_END;
  try {
    if (m_filter_false_positive_prob != 0.0)
      m_bloom_filter = new BloomFilterWithChecksum(m_trailer.filter_items_estimate,
                                                   m_filter_false_positive_prob);
    else
      m_bloom_filter = new BloomFilterWithChecksum(m_trailer.filter_items_estimate,
                                                   m_bloom_bits_per_item,
                                                   m_trailer.bloom_filter_hash_count);
  }
  catch(Exception &e) {
    HT_FATAL_OUT << "Error creating new BloomFilter for CellStore '"
                 << m_filename <<"' for "<< (is_approx ? "estimated " : "")
                 << m_trailer.filter_items_estimate << " items - "<< e << HT_END;
  }

  for (const auto &blob : *m_bloom_filter_items)
    m_bloom_filter->insert(blob.start, blob.size);

  delete m_bloom_filter_items;
  m_bloom_filter_items = 0;

  HT_DEBUG_OUT << "Created new BloomFilter for CellStore '"
    << m_filename <<"'"<< HT_END;
}

const std::vector<String> &CellStoreV5::get_replaced_files() {
  if (!m_replaced_files_loaded)
    load_replaced_files();
  return m_replaced_files;
}

void CellStoreV5::load_replaced_files() {
 bool second_try = false;
 int64_t amount = m_trailer.replaced_files_length;
 int64_t len = 0;

 try_again:

  try {
    DynamicBuffer buf(amount);

    /** Read index data **/
    len = m_filesys->pread(m_fd, buf.ptr, amount, m_trailer.replaced_files_offset, second_try);

    if (len != amount)
      HT_THROWF(Error::FSBROKER_IO_ERROR, "Error loading replaced files for "
                "CellStore '%s' : tried to read %lld but only got %lld",
                m_filename.c_str(), (Lld)amount, (Lld)len);
    /** inflate replaced files **/

    StringDecompressorPrefix decompressor;
    String filename;
    const uint8_t *ptr = buf.base;
    for (uint32_t ii=0; ii < m_trailer.replaced_files_entries; ++ii) {
      if (ptr - buf.base >= (ptrdiff_t) m_trailer.replaced_files_length)
        HT_THROWF(Error::RANGESERVER_CORRUPT_CELLSTORE,
            "Bad replaced_files_offset in CellStore trailer fd=%u replaced_files_offset=%lld, "
            "length=%llu, entries=%u, file='%s'", (unsigned)m_fd,
            (Lld)m_trailer.replaced_files_offset, (Lld)m_trailer.replaced_files_length,
            (unsigned)m_trailer.replaced_files_entries, m_filename.c_str());
      ptr = decompressor.add(ptr);
      decompressor.load(filename);
      m_replaced_files.push_back(filename);
    }
  }
  catch (Exception &e) {
    String msg;
    HT_ERROR_OUT << "pread(fd=" << m_fd << ", len=" << len << ", amount="
        << amount << ")\n" << HT_END;
    HT_ERROR_OUT << m_trailer << HT_END;
    if (second_try)
      HT_THROW2(e.code(), e, msg);
    second_try = true;
    goto try_again;
  }
  m_replaced_files_loaded = true;
}

void CellStoreV5::load_bloom_filter() {
  size_t len;

  HT_ASSERT(m_index_stats.bloom_filter_memory == 0);

  HT_DEBUG_OUT << "Loading BloomFilter for CellStore '"
               << m_filename <<"' with "<< m_trailer.filter_items_estimate
               << " items"<< HT_END;
  try {
    m_bloom_filter = new BloomFilterWithChecksum(m_trailer.filter_items_actual,
                                                 m_trailer.filter_items_actual,
                                                 m_trailer.filter_length,
                                                 m_trailer.bloom_filter_hash_count);
  }
  catch(Exception &e) {
    HT_FATAL_OUT << "Error loading BloomFilter for CellStore '"
                 << m_filename <<"' with "<< m_trailer.filter_items_estimate
                 << " items -"<< e << HT_END;
  }

  if (m_bloom_filter->total_size() > 0) {

    bool second_try = false;

    while (true) {
      try {
	len = m_filesys->pread(m_fd, m_bloom_filter->base(), m_bloom_filter->total_size(),
			       m_trailer.filter_offset, second_try);
      }
      catch (Exception &e) {
	if (!second_try) {
	  second_try=true;
	  continue;
	}
	HT_THROW2(e.code(), e, format("Error loading BloomFilter for CellStore '%s'",
				      m_filename.c_str()));
      }
      break;
    }

    if (len != m_bloom_filter->total_size())
      HT_THROWF(Error::FSBROKER_IO_ERROR, "Problem loading bloomfilter for"
                "CellStore '%s' : tried to read %lld but only got %lld",
                m_filename.c_str(), (Lld)m_bloom_filter->total_size(), (Lld)len);

    m_bytes_read += len;

    m_bloom_filter->validate(m_filename);
  }

  m_index_stats.bloom_filter_memory = sizeof(BloomFilterWithChecksum) + m_bloom_filter->total_size();
  Global::memory_tracker->add(m_index_stats.bloom_filter_memory);

}



uint64_t CellStoreV5::purge_indexes() {
  uint64_t memory_purged = 0;

  if (m_index_stats.bloom_filter_memory > 0) {
    memory_purged = m_index_stats.bloom_filter_memory;
    delete m_bloom_filter;
    m_bloom_filter = 0;
    m_index_stats.bloom_filter_memory = 0;
  }

  if (m_index_stats.block_index_memory > 0) {
    memory_purged += m_index_stats.block_index_memory;
    if (m_64bit_index)
      m_index_map64.clear();
    else
      m_index_map32.clear();
    m_index_stats.block_index_memory = 0;
  }

  Global::memory_tracker->subtract( memory_purged );

  return memory_purged;
}



void CellStoreV5::add(const Key &key, const ByteString value) {
  EventPtr event_ptr;
  DynamicBuffer zbuf;

  if (key.revision > m_trailer.revision)
    m_trailer.revision = key.revision;

  if (key.timestamp != TIMESTAMP_NULL) {
    if (key.timestamp < m_trailer.timestamp_min)
      m_trailer.timestamp_min = key.timestamp;
    else if (key.timestamp > m_trailer.timestamp_max)
      m_trailer.timestamp_max = key.timestamp;
  }

  if (m_buffer.fill() > (size_t)m_uncompressed_blocksize) {
    BlockHeaderCellStore header(BLOCK_HEADER_FORMAT, DATA_BLOCK_MAGIC);

    m_index_builder.add_entry(m_key_compressor, m_offset);

    m_uncompressed_data += (float)m_buffer.fill();
    m_compressor->deflate(m_buffer, zbuf, header, HT_DIRECT_IO_ALIGNMENT);
    m_compressed_data += (float)zbuf.fill();
    m_buffer.clear();

    uint64_t llval = ((uint64_t)m_trailer.blocksize
        * (uint64_t)m_uncompressed_data) / (uint64_t)m_compressed_data;
    m_uncompressed_blocksize = (int64_t)llval;

    if (m_outstanding_appends >= MAX_APPENDS_OUTSTANDING) {
      if (!m_sync_handler.wait_for_reply(event_ptr)) {
        if (event_ptr->type == Event::MESSAGE)
          HT_THROWF(Hypertable::Protocol::response_code(event_ptr),
             "Problem writing to FS file '%s' : %s", m_filename.c_str(),
             Hypertable::Protocol::string_format_message(event_ptr).c_str());
        HT_THROWF(event_ptr->error,
                  "Problem writing to FS file '%s'", m_filename.c_str());
      }
      m_outstanding_appends--;
    }

    if (!HT_IO_ALIGNED(zbuf.fill())) {
      memset(zbuf.ptr, 0, HT_IO_ALIGNMENT_PADDING(zbuf.fill()));
      zbuf.ptr += HT_IO_ALIGNMENT_PADDING(zbuf.fill());
    }

    size_t zlen = zbuf.fill();
    StaticBuffer send_buf(zbuf);

    try { m_filesys->append(m_fd, send_buf, Filesystem::Flags::NONE, &m_sync_handler); }
    catch (Exception &e) {
      HT_THROW2F(e.code(), e, "Problem writing to FS file '%s'",
                 m_filename.c_str());
    }
    m_outstanding_appends++;
    m_offset += zlen;
    m_key_compressor->reset();
  }

  m_key_compressor->add(key);

  size_t key_len = m_key_compressor->length();
  size_t value_len = value.length();

  m_trailer.key_bytes += key.length;
  m_trailer.value_bytes += value_len;

  if (m_column_ttl && m_column_ttl[key.column_family_code] != 0) {
    m_trailer.expirable_data += key_len + value_len;
    if ((key.timestamp + m_column_ttl[key.column_family_code]) > m_trailer.expiration_time)
      m_trailer.expiration_time = key.timestamp + m_column_ttl[key.column_family_code];
  }

  if (key.flag <= FLAG_DELETE_CELL_VERSION)
    m_trailer.delete_count++;

  m_buffer.ensure(key_len + value_len);

  m_key_compressor->write(m_buffer.ptr);
  m_buffer.ptr += key_len;

  m_buffer.add_unchecked(value.ptr, value_len);

  if (m_bloom_filter_mode != BLOOM_FILTER_DISABLED) {
    if (m_trailer.total_entries < m_max_approx_items) {
      m_bloom_filter_items->insert(key.row, key.row_len);

      if (m_bloom_filter_mode == BLOOM_FILTER_ROWS_COLS)
        m_bloom_filter_items->insert(key.row, key.row_len + 2);

      if (m_trailer.total_entries == m_max_approx_items - 1) {
        m_trailer.filter_items_estimate = (size_t)(((double)m_max_entries
            / (double)m_max_approx_items) * m_bloom_filter_items->size());
        if (m_trailer.filter_items_estimate == 0) {
          HT_INFOF("max_entries = %lld, max_approx_items = %lld, bloom_filter_items_size = %lld",
                   (Lld)m_max_entries, (Lld)m_max_approx_items, (Lld)m_bloom_filter_items->size());
          HT_ASSERT(m_trailer.filter_items_estimate);
        }
        create_bloom_filter(true);
      }
    }
    else {
      assert(!m_bloom_filter_items && m_bloom_filter);

      m_bloom_filter->insert(key.row);

      if (m_bloom_filter_mode == BLOOM_FILTER_ROWS_COLS)
        m_bloom_filter->insert(key.row, key.row_len + 2);
    }
  }

  m_trailer.total_entries++;
}


void CellStoreV5::finalize(TableIdentifier *table_identifier) {
  EventPtr event_ptr;
  size_t zlen;
  DynamicBuffer zbuf(0);
  SerializedKey key;
  StaticBuffer send_buf;
  int64_t index_memory = 0;

  if (m_buffer.fill() > 0) {
    BlockHeaderCellStore header(BLOCK_HEADER_FORMAT, DATA_BLOCK_MAGIC);

    m_index_builder.add_entry(m_key_compressor, m_offset);

    m_uncompressed_data += (float)m_buffer.fill();
    m_compressor->deflate(m_buffer, zbuf, header, HT_DIRECT_IO_ALIGNMENT);
    m_compressed_data += (float)zbuf.fill();

    if (!HT_IO_ALIGNED(zbuf.fill())) {
      memset(zbuf.ptr, 0, HT_IO_ALIGNMENT_PADDING(zbuf.fill()));
      zbuf.ptr += HT_IO_ALIGNMENT_PADDING(zbuf.fill());
    }
    zlen = zbuf.fill();
    send_buf = zbuf;

    if (m_outstanding_appends >= MAX_APPENDS_OUTSTANDING) {
      if (!m_sync_handler.wait_for_reply(event_ptr))
        HT_THROWF(Protocol::response_code(event_ptr),
                  "Problem finalizing CellStore file '%s' : %s",
                  m_filename.c_str(),
                  Protocol::string_format_message(event_ptr).c_str());
      m_outstanding_appends--;
    }

    m_filesys->append(m_fd, send_buf, Filesystem::Flags::NONE, &m_sync_handler);

    m_outstanding_appends++;
    m_offset += zlen;
  }

  m_key_compressor = 0;

  m_buffer.free();

  m_trailer.fix_index_offset = m_offset;
  if (m_uncompressed_data == 0)
    m_trailer.compression_ratio = 1.0;
  else
    m_trailer.compression_ratio = m_compressed_data / m_uncompressed_data;

  m_trailer.key_compression_scheme = KeyCompressionType::PREFIX;

  /**
   * Chop the Index buffers down to the exact length
   */
  m_index_builder.chop();

  /**
   * Write fixed index
   */
  {
    BlockHeaderCellStore header(BLOCK_HEADER_FORMAT, INDEX_FIXED_BLOCK_MAGIC);
    m_compressor->deflate(m_index_builder.fixed_buf(), zbuf, header, HT_DIRECT_IO_ALIGNMENT);
  }

  if (!HT_IO_ALIGNED(zbuf.fill())) {
    memset(zbuf.ptr, 0, HT_IO_ALIGNMENT_PADDING(zbuf.fill()));
    zbuf.ptr += HT_IO_ALIGNMENT_PADDING(zbuf.fill());
  }
  zlen = zbuf.fill();
  send_buf = zbuf;

  m_filesys->append(m_fd, send_buf, Filesystem::Flags::NONE, &m_sync_handler);

  m_outstanding_appends++;
  m_offset += zlen;

  /**
   * Write variable index
   */
  {
    BlockHeaderCellStore header(BLOCK_HEADER_FORMAT, INDEX_VARIABLE_BLOCK_MAGIC);
    m_trailer.var_index_offset = m_offset;
    m_compressor->deflate(m_index_builder.variable_buf(), zbuf, header, HT_DIRECT_IO_ALIGNMENT);
  }

  delete m_compressor;
  m_compressor = 0;

  if (!HT_IO_ALIGNED(zbuf.fill())) {
    memset(zbuf.ptr, 0, HT_IO_ALIGNMENT_PADDING(zbuf.fill()));
    zbuf.ptr += HT_IO_ALIGNMENT_PADDING(zbuf.fill());
  }
  zlen = zbuf.fill();
  send_buf = zbuf;

  m_filesys->append(m_fd, send_buf, Filesystem::Flags::NONE, &m_sync_handler);

  m_outstanding_appends++;
  m_offset += zlen;

  // write filter_offset
  m_trailer.filter_offset = m_offset;

  // if bloom_items haven't been spilled to create a bloom filter yet, do it
  m_trailer.bloom_filter_mode = BLOOM_FILTER_DISABLED;
  if (m_bloom_filter_mode != BLOOM_FILTER_DISABLED) {

    if (m_bloom_filter_items && m_bloom_filter_items->size() > 0) {
      m_trailer.filter_items_estimate = m_bloom_filter_items->size();
      create_bloom_filter();
    }

    if (m_bloom_filter) {
      m_trailer.filter_length = m_bloom_filter->get_length_bits();
      m_trailer.filter_items_actual = m_bloom_filter->get_items_actual();
      m_trailer.bloom_filter_mode = m_bloom_filter_mode;
      m_trailer.bloom_filter_hash_count = m_bloom_filter->get_num_hashes();
      m_bloom_filter->serialize(send_buf);
      m_filesys->append(m_fd, send_buf, Filesystem::Flags::NONE, &m_sync_handler);
      m_outstanding_appends++;
      m_offset += m_bloom_filter->total_size();
    }
  }

  // Write compressed replaced_file lists
  // Coalesce with trailer block if possible
  zbuf.clear();
  size_t compressed_len = 0;
  StringCompressorPrefix compressor;
  bool coalesce_with_trailer =false;
  for (size_t ii=0; ii < m_replaced_files.size();++ii) {
    compressor.add(m_replaced_files[ii].c_str());
    compressed_len += compressor.length();
  }

  if (HT_IO_ALIGNMENT_PADDING(compressed_len) >= m_trailer.size()) {
    coalesce_with_trailer = true;
    zbuf.reserve(compressed_len + m_trailer.size() +
                 HT_IO_ALIGNMENT_PADDING(compressed_len+m_trailer.size()));
  }
  else
    zbuf.reserve(compressed_len + HT_IO_ALIGNMENT_PADDING(compressed_len));
  m_trailer.replaced_files_offset = m_offset;
  m_trailer.replaced_files_entries = m_replaced_files.size();
  m_trailer.replaced_files_length = compressed_len;

  compressor.reset();
  for (size_t ii=0; ii < m_replaced_files.size();++ii) {
    compressor.add(m_replaced_files[ii].c_str());
    compressor.write(zbuf.ptr);
    zbuf.ptr += compressor.length();
  }

  if (!coalesce_with_trailer) {
    if (!HT_IO_ALIGNED(zbuf.fill())) {
      memset(zbuf.ptr, 0, HT_IO_ALIGNMENT_PADDING(zbuf.fill()));
      zbuf.ptr += HT_IO_ALIGNMENT_PADDING(zbuf.fill());
    }
    send_buf = zbuf;
    m_filesys->append(m_fd, send_buf, Filesystem::Flags::NONE, &m_sync_handler);
    m_outstanding_appends++;
    zlen = zbuf.fill();
    m_offset += zlen;
  }

  m_64bit_index = m_index_builder.big_int();

  /** Set up index **/
  if (m_64bit_index) {
    m_index_map64.load(m_index_builder.fixed_buf(),
                       m_index_builder.variable_buf(),
                       m_trailer.fix_index_offset);
    m_trailer.index_entries = m_index_map64.index_entries();
    index_memory = m_index_map64.memory_used();
    m_trailer.flags |= CellStoreTrailerV5::INDEX_64BIT;
  }
  else {
    m_index_map32.load(m_index_builder.fixed_buf(),
                       m_index_builder.variable_buf(),
                       m_trailer.fix_index_offset);
    m_trailer.index_entries = m_index_map32.index_entries();
    index_memory = m_index_map32.memory_used();
  }

  // deallocate fix index data
  m_index_builder.release_fixed_buf();

  // Add table information
  m_trailer.table_id = table_identifier->index();
  m_trailer.table_generation = table_identifier->generation;
  m_trailer.create_time = get_ts64();

  // write trailer
  if (!coalesce_with_trailer) {
    zbuf.clear();
    assert(m_trailer.size() <= HT_DIRECT_IO_ALIGNMENT);
    zbuf.reserve(HT_DIRECT_IO_ALIGNMENT);
    memset(zbuf.base, 0, HT_DIRECT_IO_ALIGNMENT);
    zbuf.ptr = zbuf.base + (HT_DIRECT_IO_ALIGNMENT-m_trailer.size());
  }
  else {
    size_t padding = HT_IO_ALIGNMENT_PADDING(m_trailer.replaced_files_length) - m_trailer.size();
    memset(zbuf.ptr, 0, padding);
    zbuf.ptr += padding;
  }
  m_trailer.serialize(zbuf.ptr);
  zbuf.ptr += m_trailer.size();

  zlen = zbuf.fill();
  send_buf = zbuf;

  m_filesys->append(m_fd, send_buf);

  m_outstanding_appends++;
  m_offset += zlen;

  /** close file for writing **/
  m_filesys->close(m_fd);

  /** Set file length **/
  m_file_length = m_offset;

  /** Re-open file for reading **/
  m_fd = m_filesys->open(m_filename, Filesystem::OPEN_FLAG_DIRECTIO);

  // If compacting due to a split, estimate the disk usage at 1/2
  if (m_trailer.flags & CellStoreTrailerV5::SPLIT)
    m_disk_usage = m_file_length / 2;
  else
    m_disk_usage = m_file_length;

  m_index_stats.block_index_memory = index_memory;

  if (m_bloom_filter)
    m_index_stats.bloom_filter_memory = sizeof(BloomFilterWithChecksum) + m_bloom_filter->total_size();

  delete [] m_column_ttl;
  m_column_ttl = 0;

  Global::memory_tracker->add( sizeof(CellStoreV5) + sizeof(CellStoreInfo) + m_index_stats.block_index_memory + m_index_stats.bloom_filter_memory );
}


void CellStoreV5::IndexBuilder::add_entry(KeyCompressorPtr &key_compressor,
                                          int64_t offset) {

  // switch to 64-bit offsets if offset being added is >= 2^32
  if (!m_bigint && offset >= 4294967296LL) {
    DynamicBuffer tmp_buf(m_fixed.size*2);
    const uint8_t *src = m_fixed.base;
    uint8_t *dst = tmp_buf.base;
    size_t remaining = m_fixed.fill();
    while (src < m_fixed.ptr)
      Serialization::encode_i64(&dst, (uint64_t)Serialization::decode_i32(&src, &remaining));
    delete [] m_fixed.release();
    m_fixed.base = tmp_buf.base;
    m_fixed.ptr = dst;
    m_fixed.size = tmp_buf.size;
    m_fixed.own = true;
    tmp_buf.release();
    m_bigint = true;
  }

  // Add key to variable buffer
  size_t key_len = key_compressor->length_uncompressed();
  m_variable.ensure(key_len);
  key_compressor->write_uncompressed(m_variable.ptr);
  m_variable.ptr += key_len;

    // Serialize offset into fix index buffer
  if (m_bigint) {
    m_fixed.ensure(8);
    memcpy(m_fixed.ptr, &offset, 8);
    m_fixed.ptr += 8;
  }
  else {
    m_fixed.ensure(4);
    memcpy(m_fixed.ptr, &offset, 4);
    m_fixed.ptr += 4;
  }
}


void CellStoreV5::IndexBuilder::chop() {
  uint8_t *base;
  size_t len;

  base = m_fixed.release(&len);
  m_fixed.reserve(len);
  m_fixed.add_unchecked(base, len);
  delete [] base;

  base = m_variable.release(&len);
  m_variable.reserve(len);
  m_variable.add_unchecked(base, len);
  delete [] base;
}



void
CellStoreV5::open(const String &fname, const String &start_row,
                  const String &end_row, int32_t fd, int64_t file_length,
                  CellStoreTrailer *trailer) {
  m_filename = fname;
  m_start_row = start_row;
  m_end_row = end_row;
  m_fd = fd;
  m_file_length = file_length;

  m_restricted_range = !(m_start_row == "" && m_end_row == Key::END_ROW_MARKER);

  m_trailer = *static_cast<CellStoreTrailerV5 *>(trailer);

  // If compacting due to a split, estimate the disk usage at 1/2
  if (m_trailer.flags & CellStoreTrailerV5::SPLIT)
    m_disk_usage = m_file_length / 2;
  else
    m_disk_usage = m_file_length;

  m_bloom_filter_mode = (BloomFilterMode)m_trailer.bloom_filter_mode;

  /** Sanity check trailer **/
  HT_ASSERT(m_trailer.version == 5);

  if (m_trailer.flags & CellStoreTrailerV5::INDEX_64BIT)
    m_64bit_index = true;

  if (!(m_trailer.fix_index_offset < m_trailer.var_index_offset &&
        m_trailer.var_index_offset < m_file_length))
    HT_THROWF(Error::RANGESERVER_CORRUPT_CELLSTORE,
              "Bad index offsets in CellStore trailer fd=%u fix=%lld, var=%lld, "
              "length=%llu, file='%s'", (unsigned)m_fd, (Lld)m_trailer.fix_index_offset,
           (Lld)m_trailer.var_index_offset, (Llu)m_file_length, fname.c_str());

  Global::memory_tracker->add( sizeof(CellStoreV5) + sizeof(CellStoreInfo) );

}


void CellStoreV5::load_block_index() {
  int64_t amount, index_amount;
  int64_t len = 0;
  BlockHeaderCellStore header(BLOCK_HEADER_FORMAT);
  SerializedKey key;
  bool inflating_fixed=true;
  bool second_try = false;

  HT_ASSERT(m_index_stats.block_index_memory == 0);

  unique_ptr<BlockCompressionCodec> compressor(create_block_compression_codec());

  amount = index_amount = m_trailer.filter_offset - m_trailer.fix_index_offset;

 try_again:

  try {
    DynamicBuffer buf(amount);

    /** Read index data **/
    len = m_filesys->pread(m_fd, buf.ptr, amount, m_trailer.fix_index_offset, second_try);

    if (len != amount)
      HT_THROWF(Error::FSBROKER_IO_ERROR, "Error loading index for "
                "CellStore '%s' : tried to read %lld but only got %lld",
                m_filename.c_str(), (Lld)amount, (Lld)len);
    /** inflate fixed index **/
    buf.ptr += (m_trailer.var_index_offset - m_trailer.fix_index_offset);
    compressor->inflate(buf, m_index_builder.fixed_buf(), header);

    m_bytes_read += m_index_builder.fixed_buf().fill();

    inflating_fixed = false;

    if (!header.check_magic(INDEX_FIXED_BLOCK_MAGIC))
      HT_THROW(Error::BLOCK_COMPRESSOR_BAD_MAGIC, m_filename);

    /** inflate variable index **/
    DynamicBuffer vbuf(0, false);
    amount = m_trailer.filter_offset - m_trailer.var_index_offset;
    vbuf.base = buf.ptr;
    vbuf.ptr = buf.ptr + amount;

    compressor->inflate(vbuf, m_index_builder.variable_buf(), header);

    m_bytes_read += m_index_builder.variable_buf().fill();

    if (!header.check_magic(INDEX_VARIABLE_BLOCK_MAGIC))
      HT_THROW(Error::BLOCK_COMPRESSOR_BAD_MAGIC, m_filename);
  }
  catch (Exception &e) {
    String msg;
    if (inflating_fixed) {
      msg = String("Error inflating FIXED index for cellstore '")
            + m_filename + "'";
      HT_ERROR_OUT << msg << ": "<< e << HT_END;
    }
    else {
      msg = "Error inflating VARIABLE index for cellstore '" + m_filename + "'";
      HT_ERROR_OUT << msg << ": " <<  e << HT_END;
    }
    HT_ERROR_OUT << "pread(fd=" << m_fd << ", len=" << len << ", amount="
        << index_amount << ")\n" << HT_END;
    HT_ERROR_OUT << m_trailer << HT_END;
    if (second_try)
      HT_THROW2(e.code(), e, msg);
    second_try = true;
    goto try_again;
  }

  /** Set up index **/
  if (m_64bit_index) {
    m_index_map64.load(m_index_builder.fixed_buf(),
                       m_index_builder.variable_buf(),
                       m_trailer.fix_index_offset, m_start_row, m_end_row);
    m_index_stats.block_index_memory = m_index_map64.memory_used();
  }
  else {
    m_index_map32.load(m_index_builder.fixed_buf(),
                       m_index_builder.variable_buf(),
                       m_trailer.fix_index_offset, m_start_row, m_end_row);
    m_index_stats.block_index_memory = m_index_map32.memory_used();
  }

  m_index_builder.release_fixed_buf();

  Global::memory_tracker->add( m_index_stats.block_index_memory );
}


bool CellStoreV5::may_contain(ScanContext *scan_ctx) {

  if (m_bloom_filter_mode == BLOOM_FILTER_DISABLED)
    return true;
  else if (m_trailer.filter_length == 0) // bloom filter is empty
    return false;
  else if (m_bloom_filter == 0)
    load_bloom_filter();

  m_index_stats.bloom_filter_access_counter = ++Global::access_counter;

  switch (m_bloom_filter_mode) {
    case BLOOM_FILTER_ROWS:
      return may_contain(scan_ctx->start_row);
    case BLOOM_FILTER_ROWS_COLS:
      if (may_contain(scan_ctx->start_row)) {
        SchemaPtr &schema = scan_ctx->schema;
        size_t rowlen = scan_ctx->start_row.length();
        boost::scoped_array<char> rowcol(new char[rowlen + 2]);
        memcpy(rowcol.get(), scan_ctx->start_row.c_str(), rowlen + 1);

        for (auto col : scan_ctx->spec->columns) {
          uint8_t column_family_id = schema->get_column_family(col)->get_id();
          rowcol[rowlen + 1] = column_family_id;

          if (may_contain(rowcol.get(), rowlen + 2))
            return true;
        }
      }
      return false;
    default:
      HT_ASSERT(!"unpossible bloom filter mode!");
  }
  return false; // silence stupid compilers
}


bool CellStoreV5::may_contain(const void *ptr, size_t len) {

  if (m_bloom_filter_mode == BLOOM_FILTER_DISABLED)
    return true;
  else if (m_trailer.filter_length == 0) // bloom filter is empty
    return false;
  else if (m_bloom_filter == 0)
    load_bloom_filter();

  m_index_stats.bloom_filter_access_counter = ++Global::access_counter;
  bool may_contain = m_bloom_filter->may_contain(ptr, len);
  return may_contain;
}



void CellStoreV5::display_block_info() {
  if (m_index_stats.block_index_memory == 0)
    load_block_index();
  if (m_64bit_index)
    m_index_map64.display();
  else
    m_index_map32.display();
}


uint16_t CellStoreV5::block_header_format() {
  return BLOCK_HEADER_FORMAT;
}
