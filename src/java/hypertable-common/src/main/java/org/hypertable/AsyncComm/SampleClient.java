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
import java.io.IOException;
import java.nio.ByteBuffer;
import java.io.FileReader;
import java.io.BufferedReader;
import java.nio.ByteOrder;
import java.nio.channels.SelectionKey;
import java.util.LinkedList;
import java.util.logging.Logger;

import org.hypertable.Common.Error;
import org.hypertable.Common.Serialization;

public class SampleClient {

    static final Logger log = Logger.getLogger("org.hypertable");

    static String usage[] = {
        "",
        "usage: sampleClient [OPTIONS] <input-file>",
        "",
        "OPTIONS:",
        "  --host=<name>  Specifies the host to connect to",
        "  --port=<n>     Specifies the port to connect to",
        "  --timeout=<t>  Specifies the connectin timeout value",
        "  --reactors=<n>  Specifies the number of reactors",
        "",
        "This is a sample program to test the AsyncComm library.  It connects",
        "to the sampleServer and sends each line of the input file",
        "to the server.  Each reply from the server is echoed to stdout.",
        "",
        null
    };

    static final int DEFAULT_PORT = 11255;

    static class ResponseHandler implements DispatchHandler {

        public ResponseHandler() {
            mConnected = false;
        }

        public void handle(Event event) {
            synchronized (this) {

                if (event.type == Event.Type.CONNECTION_ESTABLISHED) {
                    System.out.println("Connection Established.");
                    mConnected = true;
                    notify();
                }
                else if (event.type == Event.Type.DISCONNECT) {
                    if (event.error == Error.COMM_CONNECT_ERROR)
                        System.out.println("Connect error.");
                    else
                        System.out.println("Disconnect.");
                    mConnected = false;
                    notify();
                }
                else if (event.type == Event.Type.ERROR) {
                    System.err.println("Error : " + Error.GetText(event.error));
                    //exit(1);
                }
                else if (event.type == Event.Type.MESSAGE) {
                    Event newEvent = new Event(event);
                    mQueue.offer(newEvent);
                    notify();
                }
            }
        }

        public synchronized boolean WaitForConnection()
                                    throws InterruptedException {
            if (mConnected)
                return true;
            wait();
            return mConnected;
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
        private boolean           mConnected;
    }

    static boolean msShutdown = false;

    public static void main(String [] args)
                            throws InterruptedException, IOException {
        int port = DEFAULT_PORT;
        String host = "localhost";
        short reactorCount = 1;
        long timeout = 0;
        String inputFile = null;
        Event event;

        if (args.length == 0) {
            for (int i = 0; usage[i] != null; i++)
                System.out.println(usage[i]);
            System.exit(0);
        }

        for (int i=0; i<args.length; i++) {
            if (args[i].startsWith("--host="))
                host = args[i].substring(7);
            else if (args[i].startsWith("--port="))
                port = Integer.parseInt(args[i].substring(7));
            else if (args[i].startsWith("--reactors="))
                reactorCount = Short.parseShort(args[i].substring(11));
            else if (args[i].startsWith("--timeout="))
                timeout = Integer.parseInt(args[i].substring(10));
            else if (inputFile == null) {
                inputFile = args[i];
            }
            else {
                for (int j = 0; usage[j] != null; j++)
                    System.out.println(usage[j]);
                System.exit(0);
            }
        }

        if (inputFile == null) {
            for (int i = 0; usage[i] != null; i++)
                System.out.println(usage[i]);
            System.exit(0);
        }

        System.out.println("Connecting to " + host + ":" + port);

        InetSocketAddress addr;
        if (host == null)
            addr = new InetSocketAddress(port);
        else
            addr = new InetSocketAddress(host, port);

        ReactorFactory.Initialize(reactorCount);

        Comm comm = new Comm(0);

        ResponseHandler respHandler = new ResponseHandler();

        int error;
        if ((error = comm.Connect(addr, timeout, respHandler)) != Error.OK) {
            System.err.println("Connect error : " + Error.GetText(error));
            System.exit(1);
        }

        if (!respHandler.WaitForConnection())
            System.exit(1);

        CommHeader header = new CommHeader();

        int retries;
        int outstanding = 0;
        int maxOutstanding = 50;
        BufferedReader in = new BufferedReader(new FileReader(inputFile));
        String str;
        CommBuf cbuf;

        while ((str = in.readLine()) != null) {
            cbuf = new CommBuf(header, Serialization.EncodedLengthString(str));
            cbuf.AppendString(str);
            retries = 0;
            if (msShutdown == true)
                System.exit(1);
            while ((error = comm.SendRequest(addr, cbuf, respHandler))
                   != Error.OK) {
                if (msShutdown == true)
                    System.exit(1);
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
                event.payload.rewind();
                str = Serialization.DecodeString(event.payload);
                System.out.println("ECHO: '" + str + "'");
                outstanding--;
            }
        }

        while (outstanding > 0 && (event = respHandler.GetResponse()) != null) {
            event.payload.rewind();
            str = Serialization.DecodeString(event.payload);
            System.out.println("ECHO: '" + str + "'");
            outstanding--;
        }

        System.gc();

        ReactorFactory.Shutdown();
    }

}

