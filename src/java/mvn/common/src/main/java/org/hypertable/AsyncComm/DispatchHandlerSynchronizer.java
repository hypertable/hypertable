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

import java.util.LinkedList;
import java.util.logging.Logger;

public class DispatchHandlerSynchronizer implements DispatchHandler {

    static final Logger log = Logger.getLogger("org.hypertable");

    public DispatchHandlerSynchronizer(long timeoutSecs) {
        mTimeoutMs = timeoutSecs*1000;
    }

    public void handle(Event event) {
        synchronized (this) {
            mReceiveQueue.add(event);
            notify();
        }
    }

    public synchronized Event WaitForEvent() throws InterruptedException {
        if (mReceiveQueue.isEmpty())
            wait(mTimeoutMs);
        if (mReceiveQueue.isEmpty())
            return null;
        return mReceiveQueue.remove();
    }

    private LinkedList<Event> mReceiveQueue = new LinkedList<Event>();
    private long mTimeoutMs;
}
