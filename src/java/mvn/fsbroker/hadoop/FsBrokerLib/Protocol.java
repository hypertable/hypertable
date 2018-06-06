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

package org.hypertable.FsBroker.Lib;

public class Protocol extends org.hypertable.AsyncComm.Protocol {

    public static final short COMMAND_OPEN          = 0;
    public static final short COMMAND_CREATE        = 1;
    public static final short COMMAND_CLOSE         = 2;
    public static final short COMMAND_READ          = 3;
    public static final short COMMAND_APPEND         = 4;
    public static final short COMMAND_SEEK          = 5;
    public static final short COMMAND_REMOVE        = 6;
    public static final short COMMAND_SHUTDOWN      = 7;
    public static final short COMMAND_LENGTH        = 8;
    public static final short COMMAND_PREAD         = 9;
    public static final short COMMAND_MKDIRS        = 10;
    public static final short COMMAND_STATUS        = 11;
    public static final short COMMAND_FLUSH         = 12;
    public static final short COMMAND_RMDIR         = 13;
    public static final short COMMAND_READDIR       = 14;
    public static final short COMMAND_EXISTS        = 15;
    public static final short COMMAND_RENAME        = 16;
    public static final short COMMAND_DEBUG         = 17;
    public static final short COMMAND_SYNC          = 18;
    public static final short COMMAND_MAX           = 19;

    public static final short SHUTDOWN_FLAG_IMMEDIATE = 0x0001;

    public static String msCommandStrings[] = {
        "open",
        "create",
        "close",
        "read",
        "write",
        "seek",
        "remove",
        "shutdown",
        "length",
        "pread",
        "mkdirs",
        "status",
        "flush",
        "rmdir",
        "readdir",
        "exists",
        "rename",
        "debug",
        "sync"
    };

    public String CommandText(short command) {
        if (command < 0 || command >= COMMAND_MAX)
            return "UNKNOWN (" + command + ")";
        return msCommandStrings[command];
    }

};
