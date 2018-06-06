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

import java.util.HashMap;

class RequestCache {

    public static class CacheNode {
        CacheNode       prev;
        CacheNode       next;
        long            expire;
        int             id;
        IOHandlerData   handler;
        DispatchHandler dh;
    }

    public synchronized void Insert(int id, IOHandlerData handler,
                                    DispatchHandler dh, long expire) {

        CacheNode node = new CacheNode();

        node.id = id;
        node.handler = handler;
        node.dh = dh;
        node.expire = expire;

        if (mHead == null) {
            node.next = node.prev = null;
            mHead = mTail = node;
        }
        else {
            node.next = mTail;
            node.next.prev = node;
            node.prev = null;
            mTail = node;
        }

        mIdMap.put(id, node);
    }


    public synchronized CacheNode Remove(int id) {

        CacheNode node = mIdMap.remove(id);

        if (node == null) {
            //LOG_VA_DEBUG("ID %d not found in request cache", id);
            return null;
        }

        if (node.prev == null)
            mTail = node.next;
        else
            node.prev.next = node.next;

        if (node.next == null)
            mHead = node.prev;
        else
            node.next.prev = node.prev;

        if (node.handler != null)
            return node;

        return null;
    }


    public synchronized CacheNode GetNextTimeout(long now)  {

        while (mHead != null && mHead.expire < now) {
            CacheNode node = mIdMap.remove(mHead.id);
            if (node != mHead)
                throw new AssertionError("node != mHead");
            node = mHead;
            if (mHead.prev != null) {
                mHead = mHead.prev;
                mHead.next = null;
            }
            else
                mHead = mTail = null;

            if (node.handler != null)
                return node;
        }
        return null;
    }

    public synchronized void PurgeRequests(IOHandlerData handler) {
        for (CacheNode node = mTail; node != null; node = node.next) {
            if (node.handler == handler) {
                //LOG_VA_DEBUG("Purging request id %d", node.id);
                node.handler = null;  // mark for deletion
            }
        }
    }

    private HashMap<Integer, CacheNode>  mIdMap = new HashMap<Integer,
                                                      CacheNode>();
    private CacheNode mHead, mTail;

}


