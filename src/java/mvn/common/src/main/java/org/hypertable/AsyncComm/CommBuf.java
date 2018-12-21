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


package org.hypertable.AsyncComm;

import org.hypertable.Common.Serialization;

import java.io.UnsupportedEncodingException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class CommBuf {

    public CommBuf(CommHeader hdr, int len) {
        header = hdr;
        len += header.encoded_length();
        data = ByteBuffer.allocate(len);
        data.order(ByteOrder.LITTLE_ENDIAN);
        header.set_total_length(len);
        data.position( header.encoded_length() );
    }


    public CommBuf(CommHeader hdr, int len, ByteBuffer buffer) {
        header = hdr;
        ext = buffer;
        len += header.encoded_length();
        data = ByteBuffer.allocate(len);
        data.order(ByteOrder.LITTLE_ENDIAN);
        if (ext.position() > 0)
            ext.flip();
        header.set_total_length(len+ext.remaining());
        data.position( header.encoded_length() );
    }

    public CommBuf(CommHeader hdr, int len, byte [] ext_bytes, int ext_len) {
        header = hdr;
        len += header.encoded_length();
        data = ByteBuffer.allocate(len);
        data.order(ByteOrder.LITTLE_ENDIAN);
        ext = ByteBuffer.wrap(ext_bytes, 0, ext_len);
        header.set_total_length(len+ext_len);
        data.position( header.encoded_length() );
    }

    /**
     * Encodes the header at the beginning of the primary buffer and
     * resets the primary and extended data pointers to point to the
     * beginning of their respective buffers.  The AsyncComm layer
     * uses these pointers to track how much data has been sent and
     * what is remaining to be sent.
     */
    void write_header_and_reset() {
        assert !data.hasRemaining();
        data.position(0);
        header.encode(data);
        data.position(0);
    }

    /**
     * Append a boolean to the primary buffer
     */
    public void AppendBool(boolean bval) {
        data.put((bval) ? (byte)1 : (byte)0);
    }

    /**
     * Append a byte of data to the primary buffer
     */
    public void AppendByte(byte bval) { data.put(bval); }

    /**
     * Appends a byte buffer to the primary buffer.
     */
    public void AppendBytes(ByteBuffer buf) { data.put(buf); }

    /**
     * Appends a short integer (16 bit) to the the primary buffer,
     * advancing the primary buffer pointer
     */
    public void AppendShort(short sval) { data.putShort(sval); }

    /**
     * Appends an integer (32 bit) to the the primary buffer,
     * advancing the primary buffer pointer
     */
    public void AppendInt(int ival) { data.putInt(ival); }

    /**
     * Appends a long integer (64 bit) to the the primary buffer,
     * advancing the primary buffer pointer
     */
    public void AppendLong(long lval) { data.putLong(lval); }

    /**
     * Appends a byte array to the primary buffer.  A byte array
     * is encoded as a length followd by the data.
     *
     * @see Serialization::EncodeByteArray
     */
    public void AppendByteArray(byte [] bytes, int len) {
        Serialization.EncodeByteArray(data, bytes, len);
    }

    /**
     * Appends a string to the primary buffer.  A string is
     * encoded as a length, followed by the characters, followed by
     * a terminating '\\0'.
     *
     * @see Serialization::EncodeString
     */
    public void AppendString(String str) {
      Serialization.EncodeString(data, str);
    }

    public ByteBuffer data;
    public ByteBuffer ext;
    public CommHeader header;
}

