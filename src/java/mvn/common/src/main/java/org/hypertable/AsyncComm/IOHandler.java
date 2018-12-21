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

import java.net.InetSocketAddress;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.spi.AbstractSelectableChannel;
import java.util.logging.Logger;

public abstract class IOHandler {

    protected static final Logger log = Logger.getLogger("org.hypertable");

    public IOHandler(AbstractSelectableChannel chan, DispatchHandler dh,
                     ConnectionMap cm) {
        mChannel = chan;
        mDispatchHandler = dh;
        mConnMap = cm;
        mReactor = ReactorFactory.Get();
        mAddr = null;
        mInterest = 0;
    }

    public void DeliverEvent(Event event, DispatchHandler dh) {
        if (dh != null)
            dh.handle(event);
        else
            DeliverEvent(event);
    }

    public void DeliverEvent(Event event) {
        if (mDispatchHandler != null)
            mDispatchHandler.handle(event);
        else
            log.info(event.toString());
    }


    public abstract void run(SelectionKey selkey);

    public void Register(Selector selector) {
        try {
            if (mShuttingDown) {
                SelectionKey selkey = mChannel.register(selector, 0);
                selkey.cancel();
            }
            else {
                mChannel.register(selector, mInterest, this);
            }
        }
        catch (ClosedChannelException e) {
            e.printStackTrace();
            if (mAddr != null)
                mConnMap.Remove(mAddr);
        }
    }

    public void Shutdown() {
        mShuttingDown = true;
        mReactor.AddToRegistrationQueue(this);
    }

    public void SetInterest(int interest) {
        mInterest = interest;
        mReactor.AddToRegistrationQueue(this);
    }

    public void AddInterest(int interest) {
        mInterest |= interest;
        mReactor.AddToRegistrationQueue(this);
    }

    public void RemoveInterest(int interest) {
        mInterest &= ~interest;
        mReactor.AddToRegistrationQueue(this);
    }

    Reactor GetReactor() { return mReactor; }

    protected InetSocketAddress mAddr;
    protected DispatchHandler mDispatchHandler;
    protected ConnectionMap mConnMap;
    protected Reactor mReactor;
    protected AbstractSelectableChannel mChannel;
    protected int mInterest;
    protected boolean mShuttingDown = false;
}
