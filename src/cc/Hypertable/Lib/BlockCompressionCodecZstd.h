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
/// Declarations for BlockCompressionCodecZstd.
/// This file contains declarations for BlockCompressionCodecZstd, a class
/// for compressing blocks using the ZSTD compression algorithm.

#ifndef HYPERTABLE_BLOCKCOMPRESSIONCODECZSTD_H
#define HYPERTABLE_BLOCKCOMPRESSIONCODECZSTD_H

#include <Hypertable/Lib/BlockCompressionCodec.h>
extern "C" {
#include <zstd.h>
}

namespace Hypertable {

  /// @addtogroup libHypertable
  /// @{

  /// Block compressor that uses the ZSTD algorithm.
  /// This class provides a way to compress and decompress blocks of data using
  /// the <i>zstd</i> algorithm, a general purpose compression algorithm
  /// that provides good compression and decompression speed at the expense
  /// of compression size.
  class BlockCompressionCodecZstd : public BlockCompressionCodec {

  public:

    /// Constructor.
    /// This constructor passes <code>args</code> to the default implementation
    /// of set_args() since it does not support any arguments.
    /// @param args Arguments to control compression behavior
    /// @throws Exception Code set to Error::BLOCK_COMPRESSOR_INVALID_ARG
	BlockCompressionCodecZstd(const Args &args);

    /// Destructor.
    virtual ~BlockCompressionCodecZstd() {
		if (dstream != NULL)
			ZSTD_freeDStream(dstream);
		if (cstream != NULL)
			ZSTD_freeCStream(cstream);
	}

	/// Sets arguments to control compression behavior.
	/// The arguments accepted by this method are described in the following
	/// table.
	/// <table>
	/// <tr>
	/// <th>Argument</th><th>Description</th>
	/// </tr>
	/// <tr>
	/// <td><code>--best</code> or <code>--12</code> </td><td>Best compression
	/// ratio</td>
	/// </tr>
	/// <tr>
	/// <td><code>--normal</code> </td><td>Normal compression ratio</td>
	/// </tr>
	/// </table>
	/// @param args Vector of arguments
	virtual void set_args(const Args &args);

    /// Compresses a buffer using the ZSTD algorithm.
    /// This method reserves enough space in <code>output</code> to hold the
    /// serialized <code>header</code> followed by the compressed input followed
    /// by <code>reserve</code> bytes.  If the resulting compressed buffer is
    /// larger than the input buffer, then the input buffer is copied directly
    /// to the output buffer and the compression type is set to
    /// BlockCompressionCodec::NONE.  Before serailizing <code>header</code>,
    /// the <i>data_length</i>, <i>data_zlength</i>, <i>data_checksum</i>, and
    /// <i>compression_type</i> fields are set appropriately.  The output buffer
    /// is formatted as follows:
    /// <table>
    /// <tr>
    /// <td>header</td><td>compressed data</td><td>reserve</td>
    /// </tr>
    /// </table>
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param header Block header populated by function
    /// @param reserve Additional space to reserve at end of <code>output</code>
    ///   buffer
    virtual void deflate(const DynamicBuffer &input, DynamicBuffer &output,
                         BlockHeader &header, size_t reserve=0);

    /// Decompresses a buffer compressed with the ZSTD algorithm.
    /// @see deflate() for description of input buffer %format
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param header Block header
    virtual void inflate(const DynamicBuffer &input, DynamicBuffer &output,
                         BlockHeader &header);

    /// Returns enum value representing compression type ZSTD.
    /// Returns the enum value ZSTD
    /// @see BlockCompressionCodec::ZSTD
    /// @return Compression type (ZSTD)
    virtual int get_type() { return ZSTD; }

  private:

	// decompression stream
	ZSTD_DStream* const dstream = NULL;
	// compression stream
	ZSTD_CStream* const cstream = NULL;

	/// Compression level
	int m_level;
  };
  /// @}

}

#endif // HYPERTABLE_BLOCKCOMPRESSIONCODECZSTD_H

