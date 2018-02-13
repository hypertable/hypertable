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

import org.hypertable.Common.Error;
import org.hypertable.Common.Serialization;

public abstract class Protocol {

    public static int ResponseCode(Event event) {
        if (event.payload == null)
            return Error.RESPONSE_TRUNCATED;
        event.payload.rewind();
        if (event.payload.remaining() < 4)
            return Error.RESPONSE_TRUNCATED;
        return event.payload.getInt();
    }

    public static String StringFormatMessage(Event event) {
        if (event.payload == null)
            return "No message payload";
        event.payload.rewind();
        if (event.payload.remaining() < 4)
            return "Message Truncated";

        int error = event.payload.getInt();

        if (error == Error.OK)
            return Error.GetText(error);
        else {
            String str = Serialization.DecodeString(event.payload);

            if (str == null)
                return Error.GetText(error) + " - truncated";

            return Error.GetText(error) + " : " + str;
        }
    }

    public abstract String CommandText(short command);

    /**
    public static CommBuf CreateErrorMessage(HeaderBuilder hbuilder, int error,
                                             String msg) {
        CommBuf cbuf = new CommBuf(hbuilder, 4
                                   + Serialization.EncodedLengthString(msg));
        cbuf.AppendInt(error);
        cbuf.AppendString(msg);
        return cbuf;
    }
    **/

}
