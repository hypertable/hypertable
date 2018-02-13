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
package org.hypertable.Common.ClusterDefinitionFile;

import java.nio.CharBuffer;

/**
 * State of a tokenization.
 * This class is used by the Tokenizer class to help track the state 
 * of a tokenization.  The following are the key state variables that are
 * manipulated by the class's member functions.
 * <p>
 * <ul>
 *   <li>{@link TokenizerState#content} - CharBuffer containing content to be
 *       tokenized.  The buffer's position represents the beginning of the
 *       portion of the content that has not yet been tokenized</li>
 *   <li>{@link TokenizerState#offset} - Offset of the end of the current token,
 *       relative to the content buffer's position.</li>
 *   <li><code>limit</code> - Current limit of the
 *       {@link TokenizerState#content} buffer.  Member functions that
 *       advance the offset will stop when this limit is reached.</li>
 * </ul>
 * <p>
 * This class provides methods that can be used to
 * advance the offset to the end of the current token.  Once the offset has
 * been positioned at the end of the token, the token can be <i>consumed</i> via
 * a call to {@link TokenizerState#consume()}, which returns the token text and
 * then advances the content buffer's position to the beginning of the next
 * token.
 */
public class TokenizerState {

  /** Constructor.
   * @param filename Name of cluster definition file
   * @param buf Buffer holding content of file
   */
  public TokenizerState(String filename, CharBuffer buf) {
    tokenType = Token.Type.NONE;
    fname = filename;
    content = buf;
    mContentLength = content.limit();
  }

  /** Resets offset and limit.
   * Resets offset to beginning of unconsumed content and sets the
   * {@link #content} buffer's limit to the end of the original content.
   */
  public void reset() {
    offset = 0;
    content.limit(mContentLength);
    mLinesSinceLastReset = 0;
  }

  /** Sets limit to position of next newline character.
   */
  public void setLimitToLineEnd() {
    assert offset == 0;
    int new_limit = 0;
    while ((content.position() + new_limit) < content.limit() &&
           content.charAt(new_limit) != '\n')
      new_limit++;
    content.limit(content.position() + new_limit);
  }

  /** Increments offset.
   * Increments offset, keeping track of newlines encountered so that
   * {@link #line()} can properly report the current line number.
   */
  public void incrementOffset() {
    if (content.charAt(offset) == '\n')
      mLinesSinceLastReset++;
    offset++;
  }

  /** Checks if there are any characters between the offset and the
   * limit.
   * @return <i>true</i> if offset is less than content limit, <i>false</i>
   * otherwise.
   */
  public boolean hasRemaining() {
    return (content.position() + offset) < content.limit();
  }

  /** Skips offset past whitespace.
   * @return <i>true</i> if end of content was <b>not</b> reached, <i>false</i>
   * otherwise.
   */
  public boolean skipWhitespace() {
    while ((content.position() + offset) < content.limit() &&
           Character.isWhitespace(content.charAt(offset)))
      incrementOffset();
    return hasRemaining();
  }

  /** Checks for a string immediately prior to the offset.
   * Compares <code>str</code> with the string immediately before the offset.
   * @return <i>true</i> if <code>str</code> matches the characters immediately
   * before the offset, <i>false</i> otherwise.
   */
  public boolean justSeen(String str) {
    if (offset < str.length())
      return false;
    String substr = content.subSequence(offset-str.length(), offset).toString();
    return str.compareTo(substr) == 0;
  }

  /** Returns character at current offset.
   * @return Character at current offset.
   */
  public char nextChar() {
    return content.charAt(offset);
  }

  /** Returns character one position before the current offset.
   * @return Character one position before the current offset.
   */
  public char previousChar() {
    assert offset > 0;
    return content.charAt(offset-1);
  }

  /** Checks if the character at current offset is escaped.
   * Checks if the character at current offset is escaped by inspecting the
   * character before it to see if it is the backslash character.
   * @return <i>true</i> if next character is escaped, <i>false</i> otherwise.
   */
  public boolean nextCharEscaped() {
    return offset > 0 && content.charAt(offset-1) == '\\';
  }

  /** Skips offset to beginning of next line.
   * @return <i>true</i> if end of content was <b>not</b> reached, <i>false</i>
   * otherwise.
   */
  public boolean skipToNextLine() {
    while ((content.position() + offset) < content.limit() &&
           content.charAt(offset) != '\n')
      incrementOffset();
    if ((content.position() + offset) < content.limit())
      incrementOffset();
    return (content.position() + offset) < content.limit();
  }

  /** Seeks offset to next position containing a given character.
   * Seeks the offset to the next position containing the character
   * <code>c</code>.  If <code>c</code> does not exist between the offset and
   * the end of the content, then the offset is not modified and <i>false</i>
   * is returned.
   * @param c Character to seek to
   * @return <i>true</i> if character was found and offset was advanced,
   * <i>false</i> otherwise.
   */
  public boolean seekToChar(char c) {
    int lines_skipped = 0;
    int i = offset;
    while ((content.position() + i) < content.limit() &&
           content.charAt(i) != c) {
      if (content.charAt(i) == '\n')
        lines_skipped++;
      i++;
    }
    if ((content.position() + i) < content.limit()) {
      offset = i;
      mLinesSinceLastReset += lines_skipped;
      return true;
    }
    return false;
  }

  /** Consumes the current token.
   * <i>Consumes</i> the current token as follows:
   * <ol>
   *   <li>Advances {@link #content} buffer position to the beginning of the next token (or end of buffer)</li>
   *   <li>Resets the offset to 0</li>
   *   <li>Increments the line count by the number of newlines contained in the current token</li>
   *   <li>Returns the current token's text as a string</li>
   * </ol>
   * @return String containing current token text
   */
  public String consume() {
    assert offset > 0;
    assert content.limit() == mContentLength;
    String str = content.subSequence(0, offset).toString();
    content.position(content.position()+offset);
    offset = 0;
    mLine += mLinesSinceLastReset;
    mLinesSinceLastReset = 0;
    tokenType = Token.Type.NONE;
    return str;
  }

  /** Returns current absolute offset (from beginning of content).
   * @return Current absolute offset.
   */
  public int offset() {
    return content.position() + offset;
  }

  /** Returns current line offset (starting from 1).
   * @return Current line offset.
   */
  public int line() {
    return mLine;
  }

  /** Content being tokenized */
  public CharBuffer content;
  
  /** Name of file from which the content came */
  public String fname;

  /** Type of current (unconsumed) token */
  public Token.Type tokenType;

  /** Current offset of end of token */
  public int offset;

  /** Original end of content */
  private int mContentLength;

  /** Current line number */
  private int mLine = 1;

  /** Number of newline between content buffer's position and current offset */
  private int mLinesSinceLastReset;

}
