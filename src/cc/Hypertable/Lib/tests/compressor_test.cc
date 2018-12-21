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

#include <Common/Compat.h>

#include <Hypertable/Lib/CompressorFactory.h>
#include <Hypertable/Lib/BlockHeaderCommitLog.h>

#include <Common/DynamicBuffer.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>
#include <Common/System.h>
#include <Common/Usage.h>

using namespace Hypertable;

namespace {
  const char *usage[] = {
    "usage: compressor_test <type>",
    "",
    "Validates a block compressor.  The type of compressor to validate",
    "is specified by the <type> argument which can be one of:",
    "",
    "none",
    "zlib",
    "lzo",
    "quicklz",
    "snappy",
    "zstd",
    "",
    0
  };
}

const char MAGIC[12] = { '-','-','-','-','-','-','-','-','-','-','-','-' };

int main(int argc, char **argv) {
  off_t len;
  DynamicBuffer input(0);
  DynamicBuffer output1(0);
  DynamicBuffer output2(0);
  BlockCompressionCodec *compressor;

  BlockHeaderCommitLog header(MAGIC, 0, 0);

  if (argc == 1 || !strcmp(argv[1], "--help"))
    Usage::dump_and_exit(usage);

  System::initialize(System::locate_install_dir(argv[0]));

  compressor = CompressorFactory::create_block_codec(argv[1]);

  if (!compressor)
    return 1;

  if ((input.base = (uint8_t *)FileUtils::file_to_buffer("./test-schemas.xml",
      &len)) == 0) {
    HT_ERROR("Problem loading './test-schemas.xml'");
    return 1;
  }
  input.ptr = input.base + len;

  try {
    compressor->deflate(input, output1, header);
    compressor->inflate(output1, output2, header);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }

  if (input.fill() != output2.fill()) {
    HT_ERRORF("Input length (%lu) does not match output length (%lu) after %s "
              "codec", (Lu)input.fill(), (Lu)output2.fill(), argv[0]);
    return 1;
  }

  if (memcmp(input.base, output2.base, input.fill())) {
    HT_ERRORF("Input does not match output after %s codec", argv[0]);
    return 1;
  }

  // this should not compress ...

  memcpy(input.base, "foo", 3);
  input.ptr = input.base + 3;

  output2.free();

  try {
    compressor->deflate(input, output1, header);
    compressor->inflate(output1, output2, header);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }

  if (input.fill() != output2.fill()) {
    HT_ERRORF("Input length (%lu) does not match output length (%lu) after %s "
              "codec", (Lu)input.fill(), (Lu)output2.fill(), argv[0]);
    return 1;
  }

  if (memcmp(input.base, output2.base, input.fill())) {
    HT_ERRORF("Input does not match output after %s codec", argv[0]);
    return 1;
  }

  return 0;
}
