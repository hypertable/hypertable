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

package org.hypertable.examples.PerformanceTest;

import java.nio.ByteBuffer;

public class MessageFactory {

  static Message createMessage(ByteBuffer buf) {
    Message message;
    byte tb = buf.get();
    if (tb == Message.Type.REGISTER.ordinal()) {
      message = new MessageRegister();
      message.decode(buf);
      return message;
    }
    else if (tb == Message.Type.SETUP.ordinal()) {
      message = new MessageSetup();
      message.decode(buf);
      return message;
    }
    else if (tb == Message.Type.TASK.ordinal()) {
      message = new MessageTask();
      message.decode(buf);
      return message;
    }
    else if (tb == Message.Type.SUMMARY.ordinal()) {
      message = new MessageSummary();
      message.decode(buf);
      return message;
    }
    else if (tb == Message.Type.ERROR.ordinal()) {
      message = new MessageError();
      message.decode(buf);
      return message;
    }
    return new Message( Message.Type.values()[tb] );
  }
}
