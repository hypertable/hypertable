/*
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
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.PriorityQueue;
import java.util.Set;
import java.util.LinkedList;
import java.util.logging.Logger;

import org.hypertable.Common.Error;

class Reactor implements Runnable {

  final Selector selector;
  final long EVENT_LOOP_TIMEOUT_MS = 5000;

  static final Logger log = Logger.getLogger("org.hypertable");

  LinkedList<IOHandler> pending = new LinkedList<IOHandler>();

  Reactor() throws IOException {
    selector = Selector.open();
  }

  public Selector GetSelector() { return selector; }

  public synchronized void AddToRegistrationQueue(IOHandler handler) {
    pending.add(handler);
    selector.wakeup();
  }

  public void Shutdown() {
    mShutdown = true;
    selector.wakeup();
  }

  private synchronized void DoRegistrations() {
    IOHandler handler;
    while (!pending.isEmpty()) {
      handler = pending.removeFirst();
      handler.Register(selector);
    }
  }

  /** Adds a timer.
   * Adds a timer to the timer heap and then wakes up #selector so that it can
   * be called again with an updated timeout.
   * @param timer Timer to register
   */
  public synchronized void AddTimer(ExpireTimer timer) {
    mTimerHeap.add(timer);
    selector.wakeup();
  }

  /** Removes a timer.
   * Removes a timer identified by <code>handler</code>.
   * @param handler Dispatch handler of timer to remove
   */
  public synchronized void RemoveTimer(DispatchHandler handler) {
    for (ExpireTimer timer : mTimerHeap) {
      if (timer.handler == handler) {
        mTimerHeap.remove(timer);
        return;
      }
    }
  }

  /** Processes expired timers.
   * Iterates through the timer heap, removing the expired timers and calling
   * their DispatchHandler.handle() method.
   * @return Milliseconds until next timer expiration, or 0 if timer heap is
   * empty.
   */
  private long ProcessTimers() {
    ArrayList<DispatchHandler> expired_timers = new ArrayList<DispatchHandler>();
    long next = 0;
    synchronized (this) {
      if (mTimerHeap.isEmpty())
        return 0;
      long now = System.currentTimeMillis();
      while (!mTimerHeap.isEmpty()) {
        ExpireTimer timer = mTimerHeap.peek();
        if (timer.expire_time > now) {
          next = timer.expire_time - now;
          break;
        }
        mTimerHeap.remove(timer);
        expired_timers.add(timer.handler);
      }
    }
    Event event = new Event(Event.Type.TIMER, Error.OK);
    for (DispatchHandler handler : expired_timers) {
      handler.handle(event);
    }
    return next;
  }

  public void run() {
    long timeout = 0;
    try {
      Set selected;
      while (!Thread.interrupted()) {
        if (timeout == 0)
          selector.select();
        else
          selector.select(timeout);
        if (mShutdown)
          return;
        DoRegistrations();
        timeout = ProcessTimers();
        selected = selector.selectedKeys();
        Iterator iter = selected.iterator();
        while (iter.hasNext()) {
          SelectionKey selkey = (SelectionKey)(iter.next());
          IOHandler handler = (IOHandler)selkey.attachment();
          handler.run(selkey);
        }
        selected.clear();
      }
    }
    catch (IOException e) {
      e.printStackTrace();
    }
  }

  void AddRequest(int id, IOHandlerData handler, DispatchHandler dh,
                  long expire) {
    mRequestCache.Insert(id, handler, dh, expire);
  }

  DispatchHandler RemoveRequest(int id) {
    RequestCache.CacheNode node = mRequestCache.Remove(id);
    if (node != null)
      return node.dh;
    return null;
  }

  void HandleTimeouts() {
    long now = System.currentTimeMillis();
    RequestCache.CacheNode node;
    while ((node = mRequestCache.GetNextTimeout(now)) != null) {
      node.handler.DeliverEvent(new Event(Event.Type.ERROR,
                                          node.handler.GetAddress(), Error.COMM_REQUEST_TIMEOUT),
                                node.dh);
    }
  }

  void CancelRequests(IOHandlerData handler) {
    mRequestCache.PurgeRequests(handler);
  }

  private RequestCache mRequestCache = new RequestCache();
  private boolean mShutdown = false;
  private PriorityQueue<ExpireTimer> mTimerHeap = new PriorityQueue<ExpireTimer>();
}
