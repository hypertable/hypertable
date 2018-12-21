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

import java.util.concurrent.atomic.AtomicInteger;
import java.io.IOException;

public class ReactorFactory {

  public static void Initialize(short count) throws IOException {
    reactors = new Reactor [count+1];
    for (int i=0; i<=count; i++) {
      reactors[i] = new Reactor();
      Thread thread = new Thread(reactors[i], "Reactor " + i);
      thread.setPriority(Thread.MAX_PRIORITY);
      thread.start();
    }
  }

  public static void Shutdown() {
    for (int i=0; i<reactors.length; i++)
      reactors[i].Shutdown();
  }

  /** Gets a reactor.
   * Returns the next reactor in round-robin fashion from the reactors vector.
   * The last reactor in the vector is the timer reactor and is skipped.
   * @return Next reactor
   */
  public static Reactor Get() {
    return reactors[nexti.getAndIncrement() % (reactors.length-1)];
  }

  /** Gets the timer reactor.
   * Returns the timer reactor which is the last reactor in the reactors vector.
   * @return Timer reactor
   */
  public static Reactor GetTimerReactor() {
    return reactors[reactors.length-1];
  }

  /** Index of next reactor used by Get() */
  private static AtomicInteger nexti = new AtomicInteger(0);

  /** I/O Reactors */
  private static Reactor [] reactors;
}

