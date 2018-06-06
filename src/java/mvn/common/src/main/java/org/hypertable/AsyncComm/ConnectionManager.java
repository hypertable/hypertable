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
import java.util.Comparator;
import java.util.HashMap;
import java.util.PriorityQueue;
import java.util.logging.Logger;

import org.hypertable.Common.Error;

public class ConnectionManager implements Runnable, DispatchHandler {

    static final Logger log = Logger.getLogger("org.hypertable");

    private static class ConnectionState {
        public boolean             connected;
        public  InetSocketAddress  addr;
        public long                timeout;
        public long                nextRetry;
        public String              serviceName;
    };

    private static class ConnectionStateComparator implements
                         Comparator<ConnectionState> {
        public int compare(ConnectionState cs1, ConnectionState cs2) {
            if (cs1.nextRetry < cs2.nextRetry)
                return 1;
            else if (cs1.nextRetry == cs2.nextRetry)
                return 0;
            return -1;
        }
        public boolean equals(Object obj) {
            return this.equals(obj);
        }
    }

    private Comm mComm;
    private HashMap<InetSocketAddress, ConnectionState>  mConnMap;
    private PriorityQueue<ConnectionState>  mRetryQueue;
    private boolean mQuietMode = false;
    private Thread mThread;

    public ConnectionManager(Comm comm) {
        mComm = comm;
        mConnMap = new HashMap<InetSocketAddress, ConnectionState>();
        mRetryQueue = new PriorityQueue<ConnectionState>(11,
            new ConnectionStateComparator());
        mThread = new Thread(this, "Connection Manager");
        mThread.start();
    }

    public synchronized void Add(InetSocketAddress addr, long timeout,
                                 String serviceName) {
        ConnectionState connState;

        if (mConnMap.containsKey(addr))
            return;

        connState = new ConnectionState();
        connState.connected = false;
        connState.addr = addr;
        connState.timeout = timeout * 1000;
        connState.serviceName = serviceName;
        connState.nextRetry = System.currentTimeMillis();

        mConnMap.put(addr, connState);

        SendConnectRequest(connState);
    }

    /**
     */
    private void SendConnectRequest(ConnectionState connState) {

        int error = mComm.Connect(connState.addr, connState.timeout, this);

        if (error == Error.COMM_ALREADY_CONNECTED) {
            synchronized (connState) {
                connState.connected = true;
                connState.notifyAll();
            }
        }
        else if (error != Error.OK) {
            log.severe("Connection attempt to " + connState.serviceName
                       + " at " + connState.addr + " failed - "
                       + Error.GetText(error)
                       + ".  Will retry again in %d seconds...");
            connState.nextRetry = System.currentTimeMillis()
                                  + connState.timeout;
            mRetryQueue.add(connState);
            notify();
        }
    }

    public void handle(Event event) {
        synchronized (this) {
            ConnectionState connState = mConnMap.get(event.addr);

            if (connState != null) {
                synchronized (connState) {
                    if (event.type == Event.Type.CONNECTION_ESTABLISHED) {
                        connState.connected = true;
                        connState.notifyAll();
                    }
                    else {
                        if (!mQuietMode)
                            log.info(event.toString() + "; will retry in "
                                + (connState.timeout/1000) + " seconds...");
                        connState.connected = false;
                        // this logic could proably be smarter.  For example,
                        // if the last connection attempt was a long time ago,
                        // then schedule immediately otherwise, if this event
                        // is the result of an immediately prior connect
                        // attempt, then do the following
                        connState.nextRetry = System.currentTimeMillis()
                                              + connState.timeout;
                        mRetryQueue.add(connState);
                        notify();
                    }
                }
            }
            else {
                log.severe("Unable to find connection for " + event.addr
                           + " in map.");
            }
        }
    }

    public void run() {
        synchronized (this) {
            ConnectionState connState;

            while (!Thread.interrupted()) {

                try {

                    while (mRetryQueue.isEmpty())
                        wait();

                    connState = mRetryQueue.peek();

                    if (!connState.connected) {
                        synchronized (connState) {
                            long diffTime = connState.nextRetry
                                            - System.currentTimeMillis();

                            if (diffTime <= 0) {
                                mRetryQueue.remove(connState);
                                /**
                                if (!mQuietMode)
                                    log.info("Attempting to re-establish "
                                             "connection to "
                                             + connState.serviceName + " at "
                                             + connState.addr);
                                */
                                SendConnectRequest(connState);
                            }
                            else {
                                wait(diffTime);
                            }
                        }
                    }
                    else
                        mRetryQueue.remove(connState);

                }
                catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    public boolean WaitForConnection(InetSocketAddress addr, long maxWaitSecs)
                                     throws InterruptedException {
        ConnectionState connState;

        synchronized (this) {
            connState = mConnMap.get(addr);
        }

        synchronized (connState) {
            long elapsed = 0;
            long starttime = System.currentTimeMillis();
            long maxWaitMs = maxWaitSecs * 1000;

            try {

                while (!connState.connected) {
                    elapsed = System.currentTimeMillis() - starttime;
                    if (elapsed >= maxWaitMs)
                        return false;
                    connState.wait(maxWaitMs - elapsed);
                }
            }
            catch (InterruptedException e) {
                e.printStackTrace();
                return false;
            }

        }

        return true;
    }

}
