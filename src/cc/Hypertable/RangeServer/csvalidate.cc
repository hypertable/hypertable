/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
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

#include <Common/Compat.h>

#include <Hypertable/RangeServer/CellStore.h>
#include <Hypertable/RangeServer/CellStoreFactory.h>
#include <Hypertable/RangeServer/CellStoreTrailerV7.h>
#include <Hypertable/RangeServer/Config.h>
#include <Hypertable/RangeServer/Global.h>
#include <Hypertable/RangeServer/KeyDecompressorPrefix.h>

#include "Hypertable/Lib/BlockHeaderCellStore.h"
#include <Hypertable/Lib/CompressorFactory.h>
#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/LoadDataEscape.h>

#include <FsBroker/Lib/Client.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>
#include <AsyncComm/ReactorFactory.h>

#include <Common/BloomFilterWithChecksum.h>
#include <Common/ByteString.h>
#include <Common/Checksum.h>
#include <Common/InetAddr.h>
#include <Common/Init.h>
#include <Common/Logger.h>
#include <Common/Serialization.h>
#include <Common/System.h>
#include <Common/Usage.h>

#include <boost/algorithm/string.hpp>
#include <boost/any.hpp>

#include <iostream>
#include <string>
#include <vector>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace {

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc("Usage: %s [options] <filename>\n\n"
        "Dumps the contents of the CellStore contained in the FS <filename>."
        "\n\nOptions").add_options()
        ("repair", "Repair any corruption that is found")
        ;
      cmdline_hidden_desc().add_options()
      ("filename", str(), "")
      ("filename", -1);
    }
    static void init() {
      if (!has("filename")) {
        HT_ERROR_OUT <<"filename required" << HT_END;
        cout << cmdline_desc() << endl;
        exit(EXIT_FAILURE);
      }
    }
  };

  typedef Meta::list<AppPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

  class BlockEntry {
  public:
    BlockEntry() : sequence(0), offset(0), rowkey(0), matched(false), key_mismatch(false) { }
    size_t  sequence;
    int64_t offset;
    char *rowkey;
    bool matched;
    bool key_mismatch;
  };

  class State {
  public:
    State() : block_index_is_bad(false), bloom_filter_is_bad(false) { }
    String fname;
    uint8_t *base;
    uint8_t *end;
    CellStoreTrailer *trailer;
    vector<BlockEntry> index_block_info;
    vector<BlockEntry> reconstructed_block_info;
    BloomFilterWithChecksum *bloom_filter;
    BlockCompressionCodec *compressor;
    KeyDecompressor *key_decompressor;
    bool block_index_is_bad;
    bool bloom_filter_is_bad;
    uint16_t block_header_format;
  };

  //const char DATA_BLOCK_MAGIC[10]={ 'D','a','t','a','-','-','-','-','-','-' };

  const char INDEX_FIXED_BLOCK_MAGIC[10]    =
    { 'I','d','x','F','i','x','-','-','-','-' };
  const char INDEX_VARIABLE_BLOCK_MAGIC[10] =
    { 'I','d','x','V','a','r','-','-','-','-' };

  void load_file(const String &fname, State &state) {
    int64_t length = Global::dfs->length(fname.c_str());
    uint8_t *base = new uint8_t[length];
    const uint8_t *ptr = base;
    uint8_t *end = base + length;
    int fd;
    int64_t nleft = length;
    size_t nread, toread;
    uint16_t version;
    size_t remaining;

    fd = Global::dfs->open_buffered(fname.c_str(), 0, 1024*1024, 5);
    while (nleft > 0) {
      toread = (nleft > 1024*1024) ? 1024*1024 : nleft;
      nread = Global::dfs->read(fd, (uint8_t *)ptr, toread);
      nleft -= nread;
      ptr += nread;
    }

    // Read version
    ptr = end-2;
    remaining = 2;
    version = Serialization::decode_i16(&ptr, &remaining);

    if (version == 6) {
      state.trailer = new CellStoreTrailerV7();
      state.block_header_format = 0;  // hack
    }
    else {
      cout << "unsupported CellStore version (" << version << ")" << endl;
      quick_exit(EXIT_FAILURE);
    }

    state.trailer->deserialize(end - state.trailer->size());
    state.base = base;
    state.end = end;

    uint16_t compression_type = boost::any_cast<uint16_t>(state.trailer->get("compression_type"));
    state.compressor = CompressorFactory::create_block_codec((BlockCompressionCodec::Type)compression_type);
    state.key_decompressor = new KeyDecompressorPrefix();
  }
  

  void read_block_index(State &state) {
    uint32_t flags = boost::any_cast<uint32_t>(state.trailer->get("flags"));
    int64_t fix_index_offset = boost::any_cast<int64_t>(state.trailer->get("fix_index_offset"));
    int64_t var_index_offset = boost::any_cast<int64_t>(state.trailer->get("var_index_offset"));
    int64_t filter_offset = boost::any_cast<int64_t>(state.trailer->get("filter_offset"));

    BlockHeaderCellStore header(state.block_header_format);

    DynamicBuffer input_buf(0, false);
    DynamicBuffer output_buf(0, false);
    bool bits64 = (flags & CellStoreTrailerV7::INDEX_64BIT) == CellStoreTrailerV7::INDEX_64BIT;

    // FIXED
    input_buf.base = state.base + fix_index_offset;
    input_buf.ptr = input_buf.base + (var_index_offset - fix_index_offset);

    state.compressor->inflate(input_buf, output_buf, header);

    if (!header.check_magic(INDEX_FIXED_BLOCK_MAGIC)) {
      cout << "corrupt fixed index" << endl;
      quick_exit(EXIT_FAILURE);
    }

    int64_t index_entries = output_buf.fill() / (bits64 ? 8 : 4);

    state.index_block_info.reserve(index_entries);

    BlockEntry be;
    const uint8_t *ptr = (const uint8_t *)output_buf.base;
    size_t remaining = output_buf.fill();
    for (int64_t i=0; i<index_entries; i++) {
      if (bits64)
        be.offset = Serialization::decode_i64(&ptr, &remaining);
      else
        be.offset = Serialization::decode_i32(&ptr, &remaining);
      be.sequence = (size_t)i;
      state.index_block_info.push_back(be);
    }

    // VARIABLE
    input_buf.base = state.base + var_index_offset;
    input_buf.ptr = input_buf.base + (filter_offset - var_index_offset);
    output_buf.clear();

    state.compressor->inflate(input_buf, output_buf, header);

    if (!header.check_magic(INDEX_VARIABLE_BLOCK_MAGIC)) {
      cout << "corrupt variable index" << endl;
      quick_exit(EXIT_FAILURE);
    }

    SerializedKey key;

    ptr = output_buf.base;

    for (size_t i=0; i<state.index_block_info.size(); i++) {
      key.ptr = ptr;
      ptr += key.length();
      state.index_block_info[i].rowkey = (char *)key.row();
    }

  }

  void read_bloom_filter(State &state) {
    int64_t filter_offset = boost::any_cast<int64_t>(state.trailer->get("filter_offset"));
    int64_t filter_length = boost::any_cast<int64_t>(state.trailer->get("filter_length"));
    int64_t filter_items_actual = boost::any_cast<int64_t>(state.trailer->get("filter_items_actual"));
    uint8_t bloom_filter_hash_count = boost::any_cast<uint8_t>(state.trailer->get("bloom_filter_hash_count"));
    uint8_t bloom_filter_mode = boost::any_cast<uint8_t>(state.trailer->get("bloom_filter_mode"));

    if ((BloomFilterMode)bloom_filter_mode == BLOOM_FILTER_DISABLED) {
      state.bloom_filter = 0;
      return;
    }

    if ((BloomFilterMode)bloom_filter_mode == BLOOM_FILTER_ROWS_COLS) {
      cout << "Unsupported bloom filter type (BLOOM_FILTER_ROWS_COLS)" << endl;
      quick_exit(EXIT_FAILURE);
    }

    HT_ASSERT((BloomFilterMode)bloom_filter_mode == BLOOM_FILTER_ROWS);

    state.bloom_filter = new BloomFilterWithChecksum(filter_items_actual, filter_items_actual,
                                                     filter_length, bloom_filter_hash_count);
    memcpy(state.bloom_filter->base(), state.base+filter_offset, state.bloom_filter->total_size());
    try {
      state.bloom_filter->validate(state.fname);
    }
    catch (Exception &e) {
      state.bloom_filter_is_bad = true;
      state.bloom_filter = 0;
    }

  }

  template < class Operator >
  bool process_blocks (State &state, Operator op) {
    BlockHeaderCellStore header(state.block_header_format);
    int64_t offset = 0;
    int64_t end_offset = boost::any_cast<int64_t>(state.trailer->get("fix_index_offset"));
    uint32_t alignment = boost::any_cast<uint32_t>(state.trailer->get("alignment"));
    const uint8_t *ptr = state.base;
    const uint8_t *end = state.base + end_offset;
    size_t remaining;
    size_t sequence = 0;
    DynamicBuffer expand_buf(0);
    DynamicBuffer input_buf(0, false);

    while (ptr < end) {

      if ((end-ptr) < (ptrdiff_t)header.encoded_length())
        return false;

      offset = ptr - state.base;

      // decode header
      remaining = end - ptr;
      input_buf.base = input_buf.ptr = (uint8_t *)ptr;
      input_buf.size = remaining;
      header.decode((const uint8_t **)&input_buf.ptr, &remaining);

      size_t extra = 0;
      if (alignment > 0) {
        if ((header.encoded_length()+header.get_data_zlength())%alignment)
          extra = alignment - ((header.encoded_length()+header.get_data_zlength())%alignment);
      }

      // make sure we don't run off the end
      if (input_buf.ptr + header.get_data_zlength() + extra > end)
        return false;

      // Inflate block
      input_buf.ptr += header.get_data_zlength() + extra;
      state.compressor->inflate(input_buf, expand_buf, header);

      // call functor
      if (!op(sequence, offset, expand_buf))
        return false;

      ptr += input_buf.fill();
      sequence++;
    }

    return true;
  }

  void check_bloom_filter(State &state, const char *row) {
    if (state.bloom_filter && !state.bloom_filter_is_bad) {
      if (!state.bloom_filter->may_contain(row, strlen(row)))
        state.bloom_filter_is_bad = true;
    }
  }

  struct reconstruct_block_info {
    reconstruct_block_info(State &s) : state(s) { }
    bool operator()( size_t sequence, int64_t offset, DynamicBuffer &buf) {
      Key key;
      ByteString value;
      BlockEntry be;
      const uint8_t *ptr;
      const uint8_t *end = buf.base + buf.fill();

      be.sequence = sequence;
      be.offset = offset;

      state.key_decompressor->reset();
      value.ptr = state.key_decompressor->add(buf.base);
      ptr = value.ptr + value.length();
      state.key_decompressor->load(key);
      check_bloom_filter(state, key.row);

      while (ptr < end) {
        value.ptr = state.key_decompressor->add(ptr);
        state.key_decompressor->load(key);
        check_bloom_filter(state, key.row);
        ptr = value.ptr + value.length();        
      }

      if (ptr > end)
        return false;

      state.key_decompressor->load(key);
      be.rowkey = new char [ strlen(key.row)+1 ];
      strcpy(be.rowkey, key.row);
      state.reconstructed_block_info.push_back(be);
      return true;
    }
  private:
    State &state;
  };

  struct ltbe  {
    bool operator()(const BlockEntry* b1, const BlockEntry* b2) const {
      return b1->offset < b2->offset;
    }
  };

  void reconcile_block_index(State &state) {
    std::set<BlockEntry *, ltbe> bset;
    std::set<BlockEntry *, ltbe>::iterator iter;
    int64_t last_offset = 0;

    for (size_t i=0; i<state.reconstructed_block_info.size(); i++)
      bset.insert(&state.reconstructed_block_info[i]);

    for (size_t i=0; i<state.index_block_info.size(); i++) {
      if (i > 0 && state.index_block_info[i].offset <= last_offset)
        state.block_index_is_bad = true;
      iter = bset.find(&state.index_block_info[i]);
      if (iter != bset.end()) {
        (*iter)->matched = true;
        state.index_block_info[i].matched = true;
        if (strcmp((*iter)->rowkey, state.reconstructed_block_info[i].rowkey)) {
          (*iter)->key_mismatch = state.index_block_info[i].key_mismatch = true;
          state.block_index_is_bad = true;
        }
      }
      last_offset = state.index_block_info[i].offset;
    }

    for (size_t i=0; i<state.reconstructed_block_info.size(); i++) {
      if (!state.reconstructed_block_info[i].matched)
        state.block_index_is_bad = true;
      else if (state.reconstructed_block_info[i].key_mismatch)
        state.block_index_is_bad = true;        
    }    

    for (size_t i=0; i<state.index_block_info.size(); i++) {
      if (!state.index_block_info[i].matched)
        state.block_index_is_bad = true;
    }
  }

  void describe_block_index_corruption(State &state) {
    size_t key_mismatches = 0;
    int64_t last_offset = 0;

    for (size_t i=0; i<state.reconstructed_block_info.size(); i++) {
      if (!state.reconstructed_block_info[i].matched) {
        cout << "Missing block index entry (offset=" << state.reconstructed_block_info[i].offset;
        cout << ", row=" << state.reconstructed_block_info[i].rowkey << ")" << endl;
      }
      else if (state.reconstructed_block_info[i].key_mismatch) {
        key_mismatches++;
      }
    }    

    for (size_t i=0; i<state.index_block_info.size(); i++) {
      if (i > 0 && state.index_block_info[i].offset <= last_offset) {
        cout << "Out-of-order block index entry (offset=" << state.index_block_info[i].offset;
        cout << ", row=" << state.index_block_info[i].rowkey << ")" << endl;
      }
      if (!state.index_block_info[i].matched) {
        cout << "Bogus block index entry (offset=" << state.index_block_info[i].offset;
        cout << ", row=" << state.index_block_info[i].rowkey << ")" << endl;
      }
      last_offset = state.index_block_info[i].offset;
    }
  }


} // local namespace


int main(int argc, char **argv) {
  State state;

  try {
    init_with_policies<Policies>(argc, argv);

    int timeout = get_i32("timeout");
    state.fname = get_str("filename");

    cout << "Checking " << state.fname << " ... " << flush;

    ConnectionManagerPtr conn_mgr = make_shared<ConnectionManager>();

    FsBroker::Lib::ClientPtr dfs = std::make_shared<FsBroker::Lib::Client>(conn_mgr, properties);

    if (!dfs->wait_for_connection(timeout)) {
      cout << "timed out waiting for FS broker" << endl;
      quick_exit(EXIT_FAILURE);
    }

    Global::dfs = dfs;
    Global::memory_tracker = new MemoryTracker(0, 0);

    load_file(state.fname, state);
    read_block_index(state);
    read_bloom_filter(state);
    reconstruct_block_info rbi(state);
    process_blocks(state, rbi);
    reconcile_block_index(state);

    if (state.block_index_is_bad || state.bloom_filter_is_bad) {
      if (state.block_index_is_bad) {
        cout << "block index is bad" << endl;
        describe_block_index_corruption(state);
      }
      if (state.bloom_filter_is_bad)
        cout << "bloom filter is bad" << endl;
    }
    else
      cout << "valid" << endl;
    /*
    for (size_t i=0; i<state.index_block_info.size(); i++) {
      cout << state.index_block_info[i].sequence << ": " << " (" << state.index_block_info[i].offset << ") ";
      cout << state.index_block_info[i].rowkey << endl;
    }
    */

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }

  return 0;
}
