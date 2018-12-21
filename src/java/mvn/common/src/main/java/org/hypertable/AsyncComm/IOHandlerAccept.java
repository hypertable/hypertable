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

import java.nio.channels.SocketChannel;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SelectionKey;
import java.net.InetSocketAddress;
import java.net.Socket;

import org.hypertable.Common.Error;

class IOHandlerAccept extends IOHandler {

    public IOHandlerAccept(ServerSocketChannel chan, DispatchHandler dh,
                           ConnectionMap cm, ConnectionHandlerFactory chf) {
        super(chan, dh, cm);
        mServerSocketChannel = chan;
        mHandlerFactory = chf;
    }

    public void run(SelectionKey selkey) {
        try {
            if (selkey.isAcceptable()) {
                SocketChannel newChannel = mServerSocketChannel.accept();
                newChannel.configureBlocking(false);
                IOHandlerData handler = new IOHandlerData(newChannel,
                    mHandlerFactory.newInstance(), mConnMap);
                Socket socket = newChannel.socket();
                InetSocketAddress addr = new InetSocketAddress(
                    socket.getInetAddress(), socket.getPort());
                handler.SetRemoteAddress(addr);
                handler.SetInterest(SelectionKey.OP_READ);
                mConnMap.Put(addr, handler);
                DeliverEvent(new Event(Event.Type.CONNECTION_ESTABLISHED,
                                       addr, Error.OK));
            }
        }
        catch (Throwable e) {
            e.printStackTrace();
        }
    }

    private ServerSocketChannel mServerSocketChannel;
    private ConnectionHandlerFactory mHandlerFactory;
}

