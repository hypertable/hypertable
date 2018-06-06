/**
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

import java.nio.ByteBuffer;

import org.hypertable.Common.Checksum;
import org.hypertable.Common.Error;
import org.hypertable.Common.HypertableException;

public class CommHeader {

    public static byte VERSION = 1;

    public static byte FIXED_LENGTH = 38;

    public static short FLAGS_BIT_REQUEST          = (short)0x0001;
    public static short FLAGS_BIT_IGNORE_RESPONSE  = (short)0x0002;
    public static short FLAGS_BIT_URGENT           = (short)0x0004;
    public static short FLAGS_BIT_PAYLOAD_CHECKSUM = (short)0x8000;

    public static short FLAGS_MASK_REQUEST          = (short)0xFFFE;
    public static short FLAGS_MASK_IGNORE_RESPONSE  = (short)0xFFFD;
    public static short FLAGS_MASK_URGENT           = (short)0xFFFB;
    public static short FLAGS_MASK_PAYLOAD_CHECKSUM = (short)0x7FFF;

    public CommHeader() {
        version = VERSION;
        header_len = FIXED_LENGTH;
    }

    public CommHeader(long cmd, int timeout) {
        version        = VERSION;
        header_len     = FIXED_LENGTH;
        command        = cmd;
        timeout_ms = timeout;
    }

    public CommHeader(long cmd) {
        version    = VERSION;
        header_len = FIXED_LENGTH;
        command    = cmd;
    }

    public int fixed_length() { return FIXED_LENGTH; }

    public int encoded_length() { return FIXED_LENGTH; }

    public void encode(ByteBuffer buf) {
        int saved_position = buf.position();
        int checksum_position;

        buf.put(version);
        buf.put(header_len);
        buf.putShort(alignment);
        buf.putShort(flags);
        checksum_position = buf.position();
        buf.putInt(0);  // 0 for checksum computation purposes
        buf.putInt(id);
        buf.putInt(gid);
        buf.putInt(total_len);
        buf.putInt(timeout_ms);
        buf.putInt(payload_checksum);
        buf.putLong(command);

        int header_length = buf.position() - saved_position;
        byte [] header_buffer = new byte [ header_length ];
        buf.position(saved_position);
        buf.get(header_buffer, 0, header_length);
        header_checksum = Checksum.fletcher32(header_buffer, 0, header_length);
        buf.position(checksum_position);
        buf.putInt(header_checksum);
        buf.position(saved_position + header_length);
    }

    public void decode(ByteBuffer buf) throws HypertableException {
        int saved_position = buf.position();

        version = buf.get();
        header_len = buf.get();
        alignment = buf.getShort();
        flags = buf.getShort();
        buf.mark();
        header_checksum = buf.getInt();
        id = buf.getInt();
        gid = buf.getInt();
        total_len = buf.getInt();
        timeout_ms = buf.getInt();
        payload_checksum = buf.getInt();
        command = buf.getLong();

        int header_length = buf.position() - saved_position;
        buf.reset();
        buf.putInt(0);
        byte [] header_buffer = new byte [ header_length ];
        buf.position(saved_position);
        buf.get(header_buffer, 0, header_length);
        int computed_checksum = Checksum.fletcher32(header_buffer, 0,
                                                    header_length);
        if (computed_checksum != header_checksum)
            throw new HypertableException(Error.COMM_HEADER_CHECKSUM_MISMATCH);
        buf.position(saved_position + header_length);
    }

    public void set_total_length(int len) { total_len = len; }

    public void initialize_from_request_header(CommHeader req_header) {
      flags = req_header.flags;
      id = req_header.id;
      gid = req_header.gid;
      command = req_header.command;
      total_len = 0;
    }

    public byte version;
    public byte header_len;
    public short alignment;
    public short flags;
    public int header_checksum;
    public int id;
    public int gid;
    public int total_len;
    public int timeout_ms;
    public int payload_checksum;
    public long command;
}

