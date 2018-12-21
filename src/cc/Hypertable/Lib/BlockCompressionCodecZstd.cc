/* -*- c++ -*-
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

/// @file
/// Definitions for BlockCompressionCodecZstd.
/// This file contains definitions for BlockCompressionCodecZstd, a class
/// for compressing blocks using the ZSTD compression algorithm.

#include <Common/Compat.h>

#include "BlockCompressionCodecZstd.h"

#include <Common/DynamicBuffer.h>
#include <Common/Logger.h>
#include <Common/Checksum.h>

using namespace Hypertable;


BlockCompressionCodecZstd::BlockCompressionCodecZstd(const Args &args)
	: m_level(ZSTD_CLEVEL_DEFAULT) {
	set_args(args);
}

void BlockCompressionCodecZstd::set_args(const Args &args) {
	Args::const_iterator it = args.begin(), arg_end = args.end();

	for (; it != arg_end; ++it) {
		if (*it == "--ultra" || *it == "-20") {
			m_level = ZSTD_maxCLevel();
		}
		else if (*it == "--best") {
			m_level = 9;
		}
		else if (*it == "--normal") {
			m_level = ZSTD_CLEVEL_DEFAULT;
		}
		else {
			HT_THROWF(Error::BLOCK_COMPRESSOR_INVALID_ARG, "Unrecognized argument "
				"to Zstd codec: '%s'", (*it).c_str());
		}
	}
}

void
BlockCompressionCodecZstd::deflate(const DynamicBuffer &input,
    DynamicBuffer &output, BlockHeader &header, size_t reserve) {

	size_t const cBuffSize = ZSTD_compressBound(input.fill());
	output.reserve(header.encoded_length() + cBuffSize + reserve);

	size_t outlen = ZSTD_compress(
		(void *)(output.base + header.encoded_length()), cBuffSize,
		(void *)input.base, input.fill(),
		1);

	if (ZSTD_isError(outlen)) {
		HT_THROWF(Error::BLOCK_COMPRESSOR_DEFLATE_ERROR,
			"Compressed block deflate error, name=%s", ZSTD_getErrorName(outlen));
	}

  HT_ASSERT(outlen+reserve <= output.size);

  /* check for an incompressible block */
  if (outlen >= input.fill()) {
    header.set_compression_type(NONE);
    memcpy(output.base+header.encoded_length(), input.base, input.fill());
    header.set_data_length(input.fill());
    header.set_data_zlength(input.fill());
  }
  else {
    header.set_compression_type(ZSTD);
    header.set_data_length(input.fill());
    header.set_data_zlength(outlen);
  }

  header.set_data_checksum(fletcher32(output.base + header.encoded_length(),
                header.get_data_zlength()));

  output.ptr = output.base;
  header.encode(&output.ptr);
  output.ptr += header.get_data_zlength();
}


void
BlockCompressionCodecZstd::inflate(const DynamicBuffer &input,
    DynamicBuffer &output, BlockHeader &header) {
  const uint8_t *msg_ptr = input.base;
  size_t remaining = input.fill();

  header.decode(&msg_ptr, &remaining);

  if (header.get_data_zlength() > remaining)
    HT_THROWF(Error::BLOCK_COMPRESSOR_BAD_HEADER, "Block decompression error, "
              "header zlength = %lu, actual = %lu",
              (Lu)header.get_data_zlength(), (Lu)remaining);

  uint32_t checksum = fletcher32(msg_ptr, header.get_data_zlength());

  if (checksum != header.get_data_checksum())
    HT_THROWF(Error::BLOCK_COMPRESSOR_CHECKSUM_MISMATCH, "Compressed block "
              "checksum mismatch header=%lx, computed=%lx",
              (Lu)header.get_data_checksum(), (Lu)checksum);

  try {
	  output.reserve(header.get_data_length());
	  
	  // check compress bit
	  if (header.get_compression_type() == NONE)
		  memcpy(output.base, msg_ptr, header.get_data_length());
	  else {
		  size_t const dSize = ZSTD_decompress(
			  (void *)output.base,  header.get_data_length(), 
			  (void *)msg_ptr,		header.get_data_zlength());
		  
		  if (static_cast<uint32_t>(dSize) != header.get_data_length()) {
			  HT_THROWF(Error::BLOCK_COMPRESSOR_INFLATE_ERROR,
				  "Compressed block inflate error, name=%s", ZSTD_getErrorName(dSize));
		  }
	  }

    output.ptr = output.base + header.get_data_length();
  }
  catch (Exception &e) {
    output.free();
    throw;
  }
}
