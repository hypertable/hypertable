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
import java.net.ConnectException;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.SocketChannel;
import java.util.LinkedList;
import java.util.concurrent.atomic.AtomicInteger;

import org.hypertable.Common.Error;
import org.hypertable.Common.HypertableException;

class IOHandlerData extends IOHandler {

    private static AtomicInteger msNextId = new AtomicInteger(1);

    public IOHandlerData(SocketChannel chan, DispatchHandler dh,
                         ConnectionMap cm) {
        super(chan, dh, cm);
        mSocketChannel = chan;
        mHeaderBuffer = ByteBuffer.allocate(64);
        mHeaderBuffer.order(ByteOrder.LITTLE_ENDIAN);
        mSendQueue = new LinkedList<CommBuf>();
        mShutdown = false;
        reset_incoming_message_state();
    }

    public void SetRemoteAddress(InetSocketAddress addr) {
        mAddr = addr;
        mEvent.addr = addr;
    }

    public void SetTimeout(long timeout) {
      mTimeout = timeout;
    }

    InetSocketAddress GetAddress() { return mAddr; }

    private void reset_incoming_message_state() {
        mGotHeader = false;
        mEvent = new Event(Event.Type.MESSAGE, mAddr);
        mHeaderBuffer.clear();
        mHeaderBuffer.limit( mEvent.header.fixed_length() );
        mPayloadBuffer = null;
    }

    private void handle_disconnect(int error) {
        if (mAddr != null)
            mConnMap.Remove(mAddr);
        DeliverEvent(new Event(Event.Type.DISCONNECT, mAddr, error) );
        mReactor.CancelRequests(this);
        try {
            mSocketChannel.close();
        }
        catch (IOException e) {
            e.printStackTrace();
        }
    }

    public void run(SelectionKey selkey) {
        try {
            Socket socket = mSocketChannel.socket();

            if (!selkey.isValid()) {
                selkey.cancel();
                handle_disconnect(Error.COMM_BROKEN_CONNECTION);
            }

            if (selkey.isConnectable()) {
                try {
                    if (mSocketChannel.finishConnect() == false) {
                        mSocketChannel.close();
                        System.err.println("Connection error");
                        return;
                    }
                    mAddr = new InetSocketAddress(socket.getInetAddress(),
                                                  socket.getPort());
                    DeliverEvent(new Event(Event.Type.CONNECTION_ESTABLISHED,
                                           mAddr, Error.OK));
                    SetInterest(SelectionKey.OP_READ);
                    mConnMap.Put(mAddr, this);
                }
                catch (ConnectException e) {
                    selkey.cancel();
                    handle_disconnect(Error.COMM_CONNECT_ERROR);
                }
                return;
            }

            if (selkey.isReadable()) {
                int nread;
                while (true) {
                    if (!mGotHeader) {
                        nread = mSocketChannel.read(mHeaderBuffer);
                        if (nread == -1) {
                            selkey.cancel();
                            handle_disconnect(Error.COMM_BROKEN_CONNECTION);
                            return;
                        }
                        if (mHeaderBuffer.hasRemaining())
                            return;
                        handle_message_header();
                    }
                    if (mGotHeader) {
                        nread = mSocketChannel.read(mPayloadBuffer);
                        if (nread == -1) {
                            selkey.cancel();
                            handle_disconnect(Error.COMM_BROKEN_CONNECTION);
                            return;
                        }
                        if (mPayloadBuffer.hasRemaining())
                            return;
                        handle_message_body();
                    }
                }
            }

            if (selkey.isWritable()) {
                synchronized (this) {
                    if (!mSendQueue.isEmpty()) {
                        CommBuf cbuf;
                        int nwritten;
                        // try to drain the output queue
                        while (!mSendQueue.isEmpty()) {
                            cbuf = mSendQueue.peek();
                            if (!SendBuf(cbuf))
                                break;
                            mSendQueue.remove();
                        }
                        if (mSendQueue.isEmpty())
                            RemoveInterest(SelectionKey.OP_WRITE);
                    }
                }
            }
        }
        catch (Exception e) {
            try {
                selkey.cancel();
                handle_disconnect(Error.COMM_BROKEN_CONNECTION);
            }
            catch(Exception e2) {
                e2.printStackTrace();
            }
            DeliverEvent(new Event(Event.Type.DISCONNECT, mAddr,
                         Error.COMM_BROKEN_CONNECTION));
            e.printStackTrace();
        }
    }

    private void handle_message_header() throws HypertableException {

        mHeaderBuffer.position(1);
        int header_len = mHeaderBuffer.get();

        mHeaderBuffer.position( mHeaderBuffer.limit() );

        // check to see if there is any variable length header
        // after the fixed length portion that needs to be read

        if (header_len > mHeaderBuffer.limit()) {
            mHeaderBuffer.limit( header_len );
            return;
        }

        mHeaderBuffer.flip();
        mEvent.load_header(mSocketChannel.socket().getInetAddress().hashCode(),
                            mHeaderBuffer);

        mPayloadBuffer = ByteBuffer.allocate(mEvent.header.total_len
                         - header_len);
        mPayloadBuffer.order(ByteOrder.LITTLE_ENDIAN);

        mHeaderBuffer.clear();
        mGotHeader = true;
    }


    private void handle_message_body() {
        DispatchHandler dh = mReactor.RemoveRequest(mEvent.header.id);

        if ((mEvent.header.flags & CommHeader.FLAGS_BIT_REQUEST) == 0 &&
            (mEvent.header.id == 0 || dh == null)) {
            if ((mEvent.header.flags & CommHeader.FLAGS_BIT_IGNORE_RESPONSE)
                == 0) {
                log.warning("Received response for non-pending event (id=" +
                            mEvent.header.id + ",version="
                            + mEvent.header.version + ",total_len="
                            + mEvent.header.total_len + ")");
            }
            else {
                java.lang.System.out.println("nope id=" + mEvent.header.id);
            }
            mPayloadBuffer = null;
            mEvent = null;
        }
        else {
            mPayloadBuffer.flip();
            mEvent.payload = mPayloadBuffer;
            DeliverEvent( mEvent, dh );
        }

        reset_incoming_message_state();
    }


    synchronized void RegisterRequest(int id, DispatchHandler responseHandler) {
        long now  = System.currentTimeMillis();
        mReactor.AddRequest(id, this, responseHandler, now + mTimeout);
    }

    boolean SendBuf(CommBuf cbuf) throws IOException {
        int nwritten;
        if (cbuf.data != null && cbuf.data.remaining() > 0) {
            nwritten = mSocketChannel.write(cbuf.data);
            if (cbuf.data.remaining() > 0)
                return false;
        }
        if (cbuf.ext != null && cbuf.ext.remaining() > 0) {
            nwritten = mSocketChannel.write(cbuf.ext);
            if (cbuf.ext.remaining() > 0)
                return false;
        }
        return true;
    }

    synchronized int SendMessage(CommBuf cbuf) {
        CommBuf tbuf;
        boolean initiallyEmpty = mSendQueue.isEmpty();

        try {
            // try to drain the send queue
            while (!mSendQueue.isEmpty()) {
                tbuf = mSendQueue.peek();
                if (!SendBuf(tbuf))
                    break;
                mSendQueue.remove();
            }

            // try to send the message directly
            if (mSendQueue.isEmpty()) {
                if (SendBuf(cbuf)) {
                    if (!initiallyEmpty)
                        RemoveInterest(SelectionKey.OP_WRITE);
                    return Error.OK;
                }
            }

            mSendQueue.add(cbuf);
            if (initiallyEmpty)
                AddInterest(SelectionKey.OP_WRITE);
        }
        catch (Exception e) {
            e.printStackTrace();
            Shutdown();
            return Error.COMM_BROKEN_CONNECTION;
        }
        return Error.OK;
    }

    private SocketChannel mSocketChannel;
    private ByteBuffer mHeaderBuffer;
    private ByteBuffer mPayloadBuffer;
    private boolean mGotHeader;
    private LinkedList<CommBuf> mSendQueue;
    private long mTimeout;
    private boolean mShutdown;
    private Event mEvent;
}
