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

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.CharBuffer;
import java.nio.channels.FileChannel;
import java.text.ParseException;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Stack;

import org.hypertable.Common.FileUtils;

/** 
 * Transforms a cluster definition file into a sequence of tokens.
 */
public class Tokenizer {

  /** Constructor.
   * Reads data from <code>fname</code> into content buffer and initialzes state.
   * @param fname Pathname of cluster definition file
   */
  public Tokenizer(String fname) throws IOException {
    mState = new TokenizerState(fname, FileUtils.fileToCharBuffer(new File(fname)));
  }

  /** Constructor with supplied content.
   * Initializes state using <code>content</code> as content for tokenizing.
   * @param fname Pathname of cluster definition file
   * @param content Cluster definition file contents
   */
  public Tokenizer(String fname, CharBuffer content) throws IOException {
    mState = new TokenizerState(fname, content);
  }

  /** Returns the next token from the cluster definition file.
   * This reads and returns the next token from the cluster definition file.
   * @return Next token or null on EOF.
   */
  public Token next() throws ParseException {
    Token token = null;
    
    while (mState.hasRemaining()) {

      if (token == null)
        token = new Token(mState.fname, mState.line());

      if (mState.tokenType == Token.Type.NONE) {

        identifyLineType();

        if (mState.tokenType == Token.Type.VARIABLE)
          loadTokenVariable();
        else if (mState.tokenType == Token.Type.TASK)
          loadTokenFunction();
        else if (mState.tokenType == Token.Type.FUNCTION)
          loadTokenFunction();
        else if (mState.tokenType == Token.Type.CONTROLFLOW)
          loadTokenControlFlow();
        else
          mState.skipToNextLine();

      }

      if (accumulate(token))
        break;
    }

    if (token == null && mState.tokenType != Token.Type.NONE) {
      token = new Token(mState.fname, mState.line());
      token.type = mState.tokenType;
      token.append(mState.consume());
    }

    return token;
  }

  /** Loads (stages) variable definition token.
   */
  private void loadTokenVariable() throws ParseException {
    mState.skipWhitespace();
    skipIdentifier();
    assert mState.nextChar() == '=';
    mState.incrementOffset();
    if (mState.nextChar() == '\'' || mState.nextChar() == '"' || mState.nextChar() == '`') {
      if (!findEndChar())
        throw new ParseException("Unterminated string starting on line " + mState.line(),
                                 mState.offset());
    }
    mState.skipToNextLine();
  }

  /** Loads (stages) function definition token.
   */
  private void loadTokenFunction() throws ParseException {
    if (!mState.seekToChar('{'))
      throw new ParseException("Mal-formed task: statement starting on line " + mState.line(),
                                   mState.offset());
    if (!findEndChar())
      throw new ParseException("Unterminated string starting on line " + mState.line(),
                               mState.offset());
    mState.skipToNextLine();
  }

  /** Mapping from control flow statement start identifiers to end identifier.
   */
  private static final Map<String, String> gControlFlowEndTokens;

  static {
    Map<String, String> aMap = new HashMap<String, String>();
    aMap.put("if", "fi");
    aMap.put("for", "done");
    aMap.put("until", "done");
    aMap.put("while", "done");
    aMap.put("case", "esac");
    gControlFlowEndTokens = Collections.unmodifiableMap(aMap);
  }
  
  /** Loads (stages) control flow statement token.
   */
  private void loadTokenControlFlow() throws ParseException {
    Stack<String> scope = new Stack<String>();

    mState.skipWhitespace();
    
    String token = skipPastNextIdentifier();
    assert token != null;

    String endToken = gControlFlowEndTokens.get(token);
    assert endToken != null;

    scope.push(endToken);

    while ((token = skipPastNextIdentifier()) != null) {
      if (token.equals(scope.peek())) {
        scope.pop();
        if (scope.isEmpty())
          return;
      }
      else if ((endToken = gControlFlowEndTokens.get(token)) != null)
        scope.push(endToken);
    }
    mState.skipToNextLine();    
  }

  /** Checks if character is a valid identifier starting character.
   * Checks if <code>c</code> is a valid identifier starting character, which
   * can either be a letter or the '_' character.
   * @param c Character to check
   * @param <i>true</i> if <code>c</code> is a valid identifier starting
   * character, <i>false</i> otherwise.
   */
  static boolean isIdentifierStartCharacter(char c) {
    return Character.isLetter(c) || c == '_';
  }

  /** Checks if character is a valid identifier character.
   * Checks if <code>c</code> is a valid non-initial identifier character, which
   * includes letters, digits, or the '_' character.
   * @param c Character to check
   * @param <i>true</i> if <code>c</code> is a valid identifier
   * character, <i>false</i> otherwise.
   */
  static boolean isIdentifierCharacter(char c) {
    return isIdentifierStartCharacter(c) || Character.isDigit(c);
  }

  /** Skips over identifier starting at current position.
   * @return Skipped identifier
   */
  private String skipIdentifier() {
    assert isIdentifierStartCharacter(mState.nextChar());
    int starting_offset = mState.offset;
    mState.incrementOffset();
    while (mState.hasRemaining() && isIdentifierCharacter(mState.nextChar()))
      mState.incrementOffset();
    return mState.content.subSequence(starting_offset, mState.offset).toString();
  }  

  /** Skips over string or block.
   * This method must be called with the current character pointing to either a
   * string quote character (i.e. ', ", or `) or the block starting character
   * '{'.  It skips to the character immediately after the corresponding end
   * character.  It handles nested strings and blocks appropriately.
   * @return <i>true</i> if more characters remaining after the skip,
   * <i>false</i> if at EOF.
   */
  private boolean findEndChar() {
    Stack<Character> scope = new Stack<Character>();
 
    assert mState.nextChar() == '"' || mState.nextChar() == '\'' || 
      mState.nextChar() == '`' || mState.nextChar() == '{';

    scope.push(new Character(mState.nextChar()));
    mState.incrementOffset();

    while (mState.hasRemaining()) {

      if (scope.peek().charValue() == '"') {
        if (mState.nextChar() == '"' && !mState.nextCharEscaped()) {
          scope.pop();
          if (scope.empty())
            break;
        }
      }
      else if (scope.peek().charValue() == '\'') {
        if (mState.nextChar() == '\'' && !mState.nextCharEscaped()) {
          scope.pop();
          if (scope.empty())
            break;
        }
      }
      else if (scope.peek().charValue() == '`') {
        if (mState.nextChar() == '`') {
          scope.pop();
          if (scope.empty())
            break;
        }
      }
      else {
        assert scope.peek().charValue() == '{';

        if (mState.nextChar() == '#') {
          if (!mState.skipToNextLine())
            break;
        }
        else if (mState.nextChar() == '}') {
          scope.pop();
          if (scope.empty())
            break;
        }
        else if (mState.nextChar() == '"' ||
                 mState.nextChar() == '\'' ||
                 mState.nextChar() == '`')
          scope.push(new Character(mState.nextChar()));
        else if (mState.nextChar() == '{') {
          if (mState.previousChar() == '$') {
            if (!mState.seekToChar('}'))
              break;
            mState.incrementOffset();
          }
          else
            scope.push(new Character(mState.nextChar()));
        }
      }
      mState.incrementOffset();
    }
    if (mState.hasRemaining())
      mState.incrementOffset();  // skip end character
    return mState.hasRemaining();
  }

  /** Skips past the next identifier.
   * @return Next identifier, or null it none found
   */
  private String skipPastNextIdentifier() {
    char quote_char = 0;
    while (mState.hasRemaining()) {
      if (quote_char == 0) {
        if (mState.nextChar() == '"' || mState.nextChar() == '\'' || mState.nextChar() == '`')
          quote_char = mState.nextChar();
        else if (mState.nextChar() == '#') {  // skip comments
          if (!mState.skipToNextLine())
            break;
        }
        else if (isIdentifierStartCharacter(mState.nextChar()))
          return skipIdentifier();
      }
      else {
        if (mState.nextChar() == quote_char && !mState.nextCharEscaped())
          quote_char = 0;
      }
      mState.incrementOffset();
    }
    return null;
  }
  
  /** 
   */
  private void identifyLineType() throws ParseException {

    mState.setLimitToLineEnd();

    // default to token type CODE
    mState.tokenType = Token.Type.CODE;

    if (!mState.skipWhitespace())
      mState.tokenType = Token.Type.BLANKLINE;
    else if (isIdentifierStartCharacter(mState.nextChar())) {
      int id_start_offset = mState.offset;
      skipIdentifier();
      if (mState.hasRemaining()) {
        if (mState.nextChar() == '=')
          mState.tokenType = Token.Type.VARIABLE;
        else if (mState.nextChar() == ':') {
          if (mState.justSeen("include"))
            mState.tokenType = Token.Type.INCLUDE;
          else if (mState.justSeen("role"))
            mState.tokenType = Token.Type.ROLE;
          else if (mState.justSeen("task"))
            mState.tokenType = Token.Type.TASK;
          else {
            String tag = mState.content.subSequence(id_start_offset, mState.offset).toString();
            throw new ParseException("Unrecognized meta tag " + tag + " on line " + mState.line(),
                                     mState.content.position());
          }
        }
        else if (Character.isWhitespace(mState.nextChar())) {
          if (mState.justSeen("if") || mState.justSeen("while") ||
              mState.justSeen("for") || mState.justSeen("until") ||
              mState.justSeen("case")) {
            mState.tokenType = Token.Type.CONTROLFLOW;
          }
          else if (mState.justSeen("function")) {
            mState.tokenType = Token.Type.FUNCTION;
          }
          else if (mState.justSeen("role"))
            throw new ParseException("Invalid role: statement on line " + mState.line(),
                                     mState.content.position());
          else if (mState.justSeen("task"))
            throw new ParseException("Invalid task: statement on line " + mState.line(),
                                     mState.content.position());
          else if (mState.justSeen("include"))
            throw new ParseException("Invalid include: statement on line " + mState.line(),
                                     mState.content.position());
          else {
            if (!mState.skipWhitespace() || mState.nextChar() != '(')
              mState.tokenType = Token.Type.CODE;
            else
              mState.tokenType = Token.Type.FUNCTION;
          }
        }
      }
    }
    else if (mState.nextChar() == '#')
      mState.tokenType = Token.Type.COMMENT;

    mState.reset();
  }

  /** Accumulates the next token.
   */
  boolean accumulate(Token token) {

    if (token.type == Token.Type.ROLE && mState.tokenType == Token.Type.CODE)
      mState.tokenType = Token.Type.ROLE;
    else {
      if (token.type == Token.Type.COMMENT && mState.tokenType == Token.Type.TASK)
        token.type = Token.Type.TASK;
      else if (mState.tokenType == Token.Type.FUNCTION ||
               mState.tokenType == Token.Type.BLANKLINE ||
               mState.tokenType == Token.Type.CONTROLFLOW)
        mState.tokenType = Token.Type.CODE;

      if (token.type != Token.Type.NONE &&
          (token.type != mState.tokenType || mState.tokenType == Token.Type.ROLE))
        return true;
    }

    token.type = mState.tokenType;
    token.append(mState.consume());

    return token.type == Token.Type.VARIABLE || token.type == Token.Type.TASK ||
      !mState.hasRemaining();
  }

  /** State of current tokenization */
  TokenizerState mState;

}
