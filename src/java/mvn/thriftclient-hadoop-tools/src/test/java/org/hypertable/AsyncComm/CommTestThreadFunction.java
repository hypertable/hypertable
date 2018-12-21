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

import java.io.IOException;
import java.nio.ByteBuffer;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.net.InetSocketAddress;
import java.util.LinkedList;

import org.hypertable.Common.Error;
import org.hypertable.Common.Serialization;

class CommTestThreadFunction implements Runnable {

    public CommTestThreadFunction(Comm comm, InetSocketAddress addr,
                                  String input) {
        mComm = comm;
        mAddr = addr;
        mInputFile = input;
    }

    public void SetOutputFile(String output) {
        mOutputFile = output;
    }

    public void run() {
        CommBuf cbuf;
        Event event;
        CommHeader header = new CommHeader();
        ResponseHandler respHandler = new ResponseHandler();
        String str;
        int error;
        int maxOutstanding = 50;
        int outstanding = 0;
        int retries;

        try {
            BufferedReader infile  = new BufferedReader(new FileReader(
                                                        mInputFile));
            BufferedWriter outfile = new BufferedWriter(new FileWriter(
                                                        mOutputFile));

            while ((str = infile.readLine()) != null) {
                cbuf = new CommBuf(header,
                                   Serialization.EncodedLengthString(str));
                cbuf.AppendString(str);
                retries = 0;
                while ((error = mComm.SendRequest(mAddr, cbuf, respHandler))
                       != Error.OK) {
                    if (error == Error.COMM_NOT_CONNECTED) {
                        if (retries == 5) {
                            System.out.println("Connection timeout.");
                            System.exit(1);
                        }
                        Integer intObj = new Integer(0);
                        synchronized (intObj) {
                            intObj.wait(1000);
                        }
                        retries++;
                    }
                    else {
                        System.err.println("CommEngine.SendMessage returned '"
                                           + Error.GetText(error) + "'");
                        System.exit(1);
                    }
                }
                outstanding++;

                if (outstanding  > maxOutstanding) {
                    if ((event = respHandler.GetResponse()) == null)
                        break;
                    str = Serialization.DecodeString(event.payload);
                    outfile.write(str);
                    outfile.write("\n");
                    outstanding--;
                }
            }

            while (outstanding > 0 && (event = respHandler.GetResponse())
                   != null) {
                str = Serialization.DecodeString(event.payload);
                outfile.write(str);
                outfile.write("\n");
                outstanding--;
            }
            outfile.close();
            infile.close();
        }
        catch (Exception e) {
            e.printStackTrace();
        }
    }

    private static class ResponseHandler implements DispatchHandler {

        public void handle(Event event) {
            synchronized (this) {
                if (event.type == Event.Type.MESSAGE) {
                    mQueue.add(event);
                    notify();
                }
                else {
                    System.err.println(event.toString());
                    mConnected = false;
                    notify();
                }
            }
        }

        public synchronized Event GetResponse() throws InterruptedException {
            while (mQueue.isEmpty()) {
                wait();
                if (mConnected == false)
                    return null;
            }
            return mQueue.remove();
        }

        private LinkedList<Event> mQueue = new LinkedList<Event>();
        private boolean           mConnected = true;
    };

    private Comm mComm;
    private InetSocketAddress mAddr;
    String mInputFile;
    String mOutputFile;
};

