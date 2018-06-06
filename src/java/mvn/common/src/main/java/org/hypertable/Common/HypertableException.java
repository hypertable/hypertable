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

package org.hypertable.Common;

public class HypertableException extends Exception {

    public HypertableException(int _code) {
        super( Error.GetText(_code) );
        m_code = _code;
        m_msg = "";
    }

    public HypertableException(int _code, String msg) {
        super( Error.GetText(_code) + " - " + msg );
        m_code = _code;
        m_msg = msg;
    }

    int code() { return m_code; }
    String what() { return m_msg; }

    private int m_code;
    private String m_msg;

}
