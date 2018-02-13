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
import java.net.InetSocketAddress;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.concurrent.atomic.AtomicInteger;

import org.hypertable.Common.Error;

public class Comm {

  public Comm(int handlerCount) {
    if (handlerCount != 0)
      throw new AssertionError("handlerCount != 0");
    mConnMap = new ConnectionMap();
    mTimerReactor = ReactorFactory.GetTimerReactor();
  }

  public int Connect(InetSocketAddress addr, long timeout,
                     DispatchHandler defaultHandler) {
    try {
      SocketChannel channel = SocketChannel.open();
      channel.configureBlocking(false);
      channel.connect(addr);
      IOHandlerData handler = new IOHandlerData(channel, defaultHandler,
                                                mConnMap);
      handler.SetTimeout(timeout);
      handler.SetRemoteAddress(addr);
      mConnMap.Put(addr, handler);
      handler.SetInterest(SelectionKey.OP_CONNECT);
    }
    catch (IOException e) {
      e.printStackTrace();
      System.exit(-1);
    }
    return Error.OK;
  }

  public int Listen(int port, ConnectionHandlerFactory handlerFactory,
                    DispatchHandler acceptHandler) {
    try {
      ServerSocketChannel channel = ServerSocketChannel.open();
      channel.socket().setReuseAddress(true);
      channel.socket().bind(new InetSocketAddress(port));
      channel.configureBlocking(false);
      IOHandlerAccept handler = new IOHandlerAccept(channel,
                                                    acceptHandler, mConnMap, handlerFactory);
      handler.SetInterest(SelectionKey.OP_ACCEPT);
    }
    catch (IOException e) {
      e.printStackTrace();
      System.exit(-1);
    }
    return Error.OK;
  }

  private static AtomicInteger msNextId = new AtomicInteger();

  private int getUniqueId() {
    int id = msNextId.incrementAndGet();
    if (id == 0)
      id = msNextId.incrementAndGet();
    return id;
  }

  public int SendRequest(InetSocketAddress addr, CommBuf cbuf,
                         DispatchHandler responseHandler) {
    IOHandlerData handler = (IOHandlerData)mConnMap.Get(addr);
    if (handler == null)
      return Error.COMM_NOT_CONNECTED;

    cbuf.header.flags |= CommHeader.FLAGS_BIT_REQUEST;

    if (responseHandler == null) {
      cbuf.header.flags |= CommHeader.FLAGS_BIT_IGNORE_RESPONSE;
      cbuf.header.id = 0;
    }
    else {
      cbuf.header.id = getUniqueId();
      handler.RegisterRequest(cbuf.header.id, responseHandler);
    }

    cbuf.write_header_and_reset();

    int error = handler.SendMessage(cbuf);
    if (error == Error.COMM_BROKEN_CONNECTION)
      mConnMap.Remove(addr);
    return error;
  }

  public int SendResponse(InetSocketAddress addr, CommBuf cbuf) {
    IOHandlerData handler = (IOHandlerData)mConnMap.Get(addr);
    if (handler == null)
      return Error.COMM_NOT_CONNECTED;

    cbuf.header.flags &= CommHeader.FLAGS_MASK_REQUEST;

    cbuf.write_header_and_reset();

    int error = handler.SendMessage(cbuf);
    if (error == Error.COMM_BROKEN_CONNECTION)
      mConnMap.Remove(addr);
    return error;
  }

  /** Sets a timer.
   * Sets a timer to "go off" at <code>duration_millis</code> milliseconds in
   * the future.  After <code>duration_millis</code> milliseconds, the timer
   * will be removed and the DispatchHandler.handle() method of
   * <code>handler</code> will get called with a TIMER event.
   * @param duration_millis Milliseconds in the future
   * @param handler Handler through which the timer event is delivered
   */
  public void SetTimer(long duration_millis, DispatchHandler handler) {
    ExpireTimer timer = new ExpireTimer();
    timer.handler = handler;
    timer.expire_time = System.currentTimeMillis() + duration_millis;
    mTimerReactor.AddTimer(timer);
  }

  /** Sets an absolute timer.
   * Sets a timer to "go off" at the absolute time <code>expire_time</code>.
   * Once the current time is equal to <code>expire_time</code> milliseconds,
   * the timer will be removed and the DispatchHandler.handle() method of
   * <code>handler</code> will get called with a TIMER event.
   * @param duration_millis Milliseconds in the future
   * @param handler Handler through which the timer event is delivered
   */
  public void SetTimerAbsolute(long expire_time, DispatchHandler handler) {
    ExpireTimer timer = new ExpireTimer();
    timer.handler = handler;
    timer.expire_time = expire_time;
    mTimerReactor.AddTimer(timer);
  }

  /** Cancels a timer.
   * Cancels the timer previously registered with <code>handler</code>.
   * @param handler Handler identifying timer to cancel.
   */
  public void CancelTimer(DispatchHandler handler) {
    mTimerReactor.RemoveTimer(handler);
  }

  public ConnectionMap mConnMap;

  /** Timer reactor. */
  private Reactor mTimerReactor;
}

