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

package org.hypertable.FsBroker.Lib;

import org.hypertable.AsyncComm.Comm;
import org.hypertable.AsyncComm.CommBuf;
import org.hypertable.AsyncComm.CommHeader;
import org.hypertable.AsyncComm.Event;
import org.hypertable.AsyncComm.ResponseCallback;
import org.hypertable.Common.Error;
import org.hypertable.Common.Serialization;
import org.hypertable.Common.Status;

import java.io.UnsupportedEncodingException;
import java.util.logging.Logger;

public class ResponseCallbackStatus extends ResponseCallback {

  static final Logger log = Logger.getLogger("org.hypertable");

  ResponseCallbackStatus(Comm comm, Event event) {
    super(comm, event);
  }

  static final byte VERSION = 1;

  public int response(Status status) {
    try {
      org.hypertable.FsBroker.Lib.Response.Parameters.Status params =
        new org.hypertable.FsBroker.Lib.Response.Parameters.Status(status);
      CommHeader header = new CommHeader();
      header.initialize_from_request_header(mEvent.header);
      CommBuf cbuf = new CommBuf(header, 4 + params.encodedLength());
      cbuf.AppendInt(Error.OK);
      params.encode(cbuf.data);
      return mComm.SendResponse(mEvent.addr, cbuf);
    }
    catch (UnsupportedEncodingException e) {
      log.severe("Unsupported encoding exception caught in Status callback!");
      System.exit(-1);
    }
    return Error.PROTOCOL_ERROR;
  }
}

