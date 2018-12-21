/** -*- c++ -*-
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

#include "CompressorFactory.h"

#include <Common/Logger.h>

#include <Hypertable/Lib/BlockCompressionCodecBmz.h>
#include <Hypertable/Lib/BlockCompressionCodecNone.h>
#include <Hypertable/Lib/BlockCompressionCodecZlib.h>
#include <Hypertable/Lib/BlockCompressionCodecLzo.h>
#include <Hypertable/Lib/BlockCompressionCodecQuicklz.h>
#include <Hypertable/Lib/BlockCompressionCodecSnappy.h>
#include <Hypertable/Lib/BlockCompressionCodecZstd.h>

#include <boost/algorithm/string.hpp>

using namespace Hypertable;
using namespace std;
using namespace boost;

BlockCompressionCodec::Type
CompressorFactory::parse_block_codec_spec(const std::string &spec,
                                          BlockCompressionCodec::Args &args) {
  string name;

  split(args, spec, is_any_of(" \t"), token_compress_on);

  if (!args.empty()) {
    name = args.front();
    args.erase(args.begin());
  }

  if (name == "none" || name.empty())
    return BlockCompressionCodec::NONE;

  if (name == "bmz")
    return BlockCompressionCodec::BMZ;

  if (name == "zlib")
    return BlockCompressionCodec::ZLIB;

  if (name == "lzo")
    return BlockCompressionCodec::LZO;

  if (name == "quicklz")
    return BlockCompressionCodec::QUICKLZ;

  if (name == "snappy")
    return BlockCompressionCodec::SNAPPY;

  if (name == "zstd")
    return BlockCompressionCodec::ZSTD;

  HT_ERRORF("unknown codec type: %s", name.c_str());
  return BlockCompressionCodec::UNKNOWN;
}

BlockCompressionCodec *
CompressorFactory::create_block_codec(BlockCompressionCodec::Type type,
                                      const BlockCompressionCodec::Args &args) {
  switch (type) {
  case BlockCompressionCodec::BMZ:
    return new BlockCompressionCodecBmz(args);
  case BlockCompressionCodec::NONE:
    return new BlockCompressionCodecNone(args);
  case BlockCompressionCodec::ZLIB:
    return new BlockCompressionCodecZlib(args);
  case BlockCompressionCodec::LZO:
    return new BlockCompressionCodecLzo(args);
  case BlockCompressionCodec::QUICKLZ:
    return new BlockCompressionCodecQuicklz(args);
  case BlockCompressionCodec::SNAPPY:
    return new BlockCompressionCodecSnappy(args);
  case BlockCompressionCodec::ZSTD:
    return new BlockCompressionCodecZstd(args);
  default:
    HT_THROWF(Error::BLOCK_COMPRESSOR_UNSUPPORTED_TYPE, "Invalid compression "
              "type: '%d'", (int)type);
  }
}
