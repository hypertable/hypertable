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

import java.net.InetSocketAddress;

import org.hypertable.Common.Error;
import org.hypertable.Common.Serialization;

/**
 *
 */
public class ResponseCallback {

    public ResponseCallback(Comm comm, Event event) {
        mComm = comm;
        mEvent = event;
    }

    public InetSocketAddress GetAddress() {
        return mEvent.addr;
    }

    public int error(int error, String msg) {
        CommHeader header = new CommHeader();
        header.initialize_from_request_header(mEvent.header);
        CommBuf cbuf = new CommBuf(header, 4
                                   + Serialization.EncodedLengthString(msg));
        cbuf.AppendInt(error);
        cbuf.AppendString(msg);
        return mComm.SendResponse(mEvent.addr, cbuf);
    }

    public int response_ok() {
        CommHeader header = new CommHeader();
        header.initialize_from_request_header(mEvent.header);
        CommBuf cbuf = new CommBuf(header, 4);
        cbuf.AppendInt(Error.OK);
        return mComm.SendResponse(mEvent.addr, cbuf);
    }

    public int request_ttl() {
        return mEvent.header.timeout_ms;
    }

    protected Comm mComm;
    protected Event mEvent;
}
