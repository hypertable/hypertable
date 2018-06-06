/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
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

package org.hypertable.Common.ClusterDefinitionFile;

import java.lang.StringBuilder;

/**
 * Token used in the lexical analysis of a cluster definition file (<code>cluster.def</code>).
 */

public class Token {

  /** Enumeration for token types. */
  public enum Type {
    /** none */
    NONE(0),
    /** include statement */
    INCLUDE(1),
    /** variable defintion */
    VARIABLE(2),
    /** role statement */
    ROLE(3),
    /** task statement */
    TASK(4),
    /** function defintion */
    FUNCTION(5),
    /** control flow statement (if, for, until, while, case) */
    CONTROLFLOW(6),
    /** comment block */
    COMMENT(7),
    /** code block */
    CODE(8),
    /** blank line */
    BLANKLINE(9);

    /** Gets the underlying integer value.
     * @return Underlying integer value
     */
    public int getValue() {
      return value;
    }

    /** Integer value */
    private final int value;

    /** Constructor.
     * @param value Integer value for this enumeration constant
     */
    private Type(int value) {
      this.value = value;
    }

  }

  /** Constructor.
   * @param filename Name of file in which this token appears
   * @param lineno Line number on which the start of the token appears
   */
  public Token(String filename, int lineno) {
    fname = filename;
    line = lineno;
  }

  /** Appends string to token text.
   * @param str String
   */
  public void append(String str) {
    mText.append(str);
  }

  /** Gets token text.
   * @return Token text
   */
  public String text() {
    return mText.toString();
  }

  /** Gets token type
   * @return Token type
   */
  public Type type() {
    return type;
  }

  /** Token type */
  Type type = Type.NONE;

  /** Starting line number of token */
  int line;

  /** Pathname of file from which token was extracted */
  String fname;

  /** Token text */
  private StringBuilder mText = new StringBuilder();

}
