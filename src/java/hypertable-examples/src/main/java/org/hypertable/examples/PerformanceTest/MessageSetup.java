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
import java.nio.ByteOrder;

public class MessageSetup extends Message {

  public MessageSetup() {
    super(Message.Type.SETUP);
  }

  public MessageSetup(Setup t) {
    super(Message.Type.SETUP);
    setup = t;
  }

  public int encodedLength() { return setup.encodedLength(); }

  public void encode(ByteBuffer buf) {
    setup.encode(buf);
  }

  public void decode(ByteBuffer buf) {
    setup = new Setup();
    setup.decode(buf);
  }

  public String toString() {
    return new String("MESSAGE:SETUP {" + setup + "}");
  }

  public Setup setup;
}

