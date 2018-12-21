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

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.LinkedList;
import java.util.logging.Level;
import java.util.logging.Logger;

import org.hypertable.Common.Error;

public class SampleServer {

    static final Logger log = Logger.getLogger("org.hypertable");

    private static class RequestHandler implements DispatchHandler {

        public void handle(Event event) {
            if (event.type == Event.Type.CONNECTION_ESTABLISHED)
                log.info("Connection Established.");
            else if (event.type == Event.Type.DISCONNECT) {
                if (event.error == Error.COMM_CONNECT_ERROR)
                    log.info("Connect error.");
                else
                    log.info("Disconnect.");
            }
            else if (event.type == Event.Type.ERROR) {
                log.info("Error : " + Error.GetText(event.error));
                System.exit(1);
            }
            else if (event.type == Event.Type.MESSAGE) {
                synchronized (this) {
                    Event newEvent = new Event(event);
                    mQueue.offer(newEvent);
                    notify();
                }
            }
        }

        public synchronized Event GetRequest() throws InterruptedException {
            while (mQueue.isEmpty()) {
                wait();
            }
            if (gDelay > 0)
                wait(gDelay);
            return mQueue.remove();
        }

        private LinkedList<Event> mQueue = new LinkedList<Event>();
    }

    private static class HandlerFactory implements ConnectionHandlerFactory {
        public HandlerFactory(DispatchHandler cb) {
            mDispatchHandler = cb;
        }
        public DispatchHandler newInstance() {
            return mDispatchHandler;
        }
        private DispatchHandler mDispatchHandler;
    }


    static String usage[] = {
        "",
        "usage: sampleServer [OPTIONS]",
        "",
        "OPTIONS:",
        "  --help          Display this help text and exit",
        "  --port=<n>      Specifies the port to connect to",
        "  --reactors=<n>  Specifies the number of reactors",
        "  --delay=<ms>    Specifies milliseconds to wait before echoing ",
        "                  message (default=0)",
        "",
        "This is a sample program to test the AsyncComm library.  It connects",
        "to the sampleServer and sends each line of the input file",
        "to the server.  Each reply from the server is echoed to stdout.",
        "",
        null
    };


    static final int DEFAULT_PORT = 11255;
    static int gDelay = 0;

    public static void main(String [] args)
                            throws InterruptedException, IOException {
        int port = DEFAULT_PORT;
        short reactorCount = 1;
        Event event;
        HandlerFactory handlerFactory;
        CommHeader header;

        if (args.length == 1 && args[0].equals("--help")) {
            for (int i = 0; usage[i] != null; i++)
                System.out.println(usage[i]);
            System.exit(0);
        }

        for (int i=0; i<args.length; i++) {
            if (args[i].startsWith("--port="))
                port = Integer.parseInt(args[i].substring(7));
            else if (args[i].startsWith("--delay="))
                gDelay = Integer.parseInt(args[i].substring(8));
            else if (args[i].startsWith("--reactors="))
                reactorCount = Short.parseShort(args[i].substring(11));
            else {
                for (int j = 0; usage[j] != null; j++)
                    System.out.println(usage[j]);
                System.exit(0);
            }
        }

        ReactorFactory.Initialize(reactorCount);

        RequestHandler requestHandler = new RequestHandler();

        handlerFactory = new HandlerFactory(requestHandler);

        Comm comm = new Comm(0);
        CommBuf cbuf;

        comm.Listen(port, handlerFactory, requestHandler);

        while ((event = requestHandler.GetRequest()) != null) {
            header = new CommHeader();
            header.initialize_from_request_header(event.header);
            cbuf = new CommBuf(header, event.payload.remaining());
            cbuf.AppendBytes(event.payload);
            int error = comm.SendResponse(event.addr, cbuf);
            if (error != Error.OK)
                log.log(Level.SEVERE, "Comm.SendResponse returned "
                        + Error.GetText(error));
        }

    }

}

