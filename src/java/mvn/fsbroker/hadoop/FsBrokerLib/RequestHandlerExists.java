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

import java.net.ProtocolException;
import java.util.logging.Logger;
import org.hypertable.AsyncComm.ApplicationHandler;
import org.hypertable.AsyncComm.Comm;
import org.hypertable.Common.Serialization;
import org.hypertable.AsyncComm.Event;
import org.hypertable.Common.Error;

public class RequestHandlerExists extends ApplicationHandler {

  static final Logger log = Logger.getLogger("org.hypertable");

  static final byte VERSION = 1;

  public RequestHandlerExists(Comm comm, Broker broker, Event event) {
    super(event);
    mComm = comm;
    mBroker = broker;
  }

  public void run() {
    String  fileName;
    ResponseCallbackExists cb = new ResponseCallbackExists(mComm, mEvent);

    try {

      if (mEvent.payload.remaining() < 2)
        throw new ProtocolException("Truncated message");

      int version = (int)mEvent.payload.get();
      if (version != VERSION)
        throw new ProtocolException("Exists parameters version mismatch, expected " +
                                    VERSION + ", got " + version);

      int encoding_length = Serialization.DecodeVInt32(mEvent.payload);
      int start_position = mEvent.payload.position();

      if ((fileName = Serialization.DecodeVStr(mEvent.payload)) == null)
        throw new ProtocolException("Filename not properly encoded in request packet");

      if ((mEvent.payload.position() - start_position) < encoding_length)
        mEvent.payload.position(start_position + encoding_length);

      mBroker.Exists(cb, fileName);

    }
    catch (Exception e) {
      int error = cb.error(Error.PROTOCOL_ERROR, e.getMessage());
      log.severe("Protocol error (EXISTS) - " + e.getMessage());
      if (error != Error.OK)
        log.severe("Problem sending (EXISTS) error back to client - "
                   + Error.GetText(error));
    }
  }

  private Comm mComm;
  private Broker mBroker;
}
