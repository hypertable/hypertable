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
import java.nio.ByteBuffer;
import java.util.Date;
import java.util.concurrent.ConcurrentLinkedQueue;

import org.hypertable.Common.Error;
import org.hypertable.Common.HypertableException;

public class Event {

    public enum Type { CONNECTION_ESTABLISHED, DISCONNECT, MESSAGE, ERROR,
                       TIMER }

    public Event(Event other) {
        type = other.type;
        addr = other.addr;
        error = other.error;
        header = other.header;
        payload = other.payload;
        thread_group = other.thread_group;
    }

    /** Initializes the event object.
     *
     * @param ct type of event
     * @param a remote address from which event originated
     * @param err error code associated with this event
     */
    public Event(Type ct, InetSocketAddress a, int err) {
        type = ct;
        addr = a;
        error = err;
    }

    /** Initializes the event object.
     *
     * @param ct type of event
     * @param a remote address from which event originated
     */
    public Event(Type ct, InetSocketAddress a) {
        type = ct;
        addr = a;
    }

    /** Initializes the event object.
     *
     * @param ct type of event
     * @param err error code associated with this event
     */
    Event(Type ct, int err) {
        type = ct;
        error = err;
    }

    /** Loads header object from serialized buffer.  This method
     * also sets the thread_group member.
     *
     * @param sd socket descriptor from which the event was generated
     *        (used for thread_group)
     * @param buf byte buffer containing serialized header
     */
    void load_header(int sd, ByteBuffer buf) throws HypertableException {
        header.decode(buf);
        if (header.gid != 0)
            thread_group = ((long)sd << 32) | header.gid;
        else
            thread_group = 0;
    }

    public String toString() {
        String str = "[" + addr + " ; " + (new Date()).toString() + "] ";
        if (type == Event.Type.CONNECTION_ESTABLISHED)
            str += "Connection Established";
        else if (type == Event.Type.DISCONNECT)
            str += "Disconnect";
        else if (type == Event.Type.MESSAGE)
            str += "Message";
        else if (type == Event.Type.ERROR)
            str += "Error";
        if (error != Error.OK) {
            str += " - " + Error.GetText(error);
        }
        return str;
    }

    public Type type;
    public InetSocketAddress addr;
    public int    error;
    public CommHeader header = new CommHeader();
    public ByteBuffer payload;
    public long thread_group;
}
