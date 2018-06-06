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
package org.hypertable.Common;

import java.lang.Integer;
import java.lang.String;
import java.text.ParseException;
import java.util.Comparator;
import java.util.Stack;
import java.util.TreeSet;
import java.util.Vector;

public class HostSpecification {

  public static class ClusterHostname {

    /**
     * Constructor.
     * @param full Full host name
     * @param pre Prefix of host name *before* numeric component
     * @param n Numeric component
     * @param post Postfix of host name *after* numeric component
     */
    public ClusterHostname(String full, String pre, int n, String post) {
      name = full;
      prefix = pre;
      num = n;
      postfix = post;
    }

    /**
     * Constructor.
     * Parses <code>full</code> to extract #prefix, #postfix, and #num.  The
     * numeric component is assumed to be the first string of digits found in
     * <code>full</code>.
     * @param full Full host name
     */
    public ClusterHostname(String full) {
      name = full;
      int i = 0;
      while (i < name.length() && (name.charAt(i) < '0' || name.charAt(i) > '9'))
        i++;
      prefix = name.substring(0, i);
      if (i < name.length()) {
        int base = i;
        while (i < name.length() && name.charAt(i) >= '0' && name.charAt(i) <= '9')
          i++;
        num = Integer.parseInt(name.substring(base, i));
        postfix = name.substring(i);
      }
    }

    /** Full hostname */
    public String name;
    /** Prefix of host name *before* numeric component */
    public String prefix;
    /** Postfix of host name *after* numeric component */
    public String postfix;
    /** Numeric component */
    public int num = -1;
  }

  public static class ClusterHostnameComparator implements Comparator<ClusterHostname> {    
    @Override
    public int compare(ClusterHostname lhs, ClusterHostname rhs) {
      if (lhs.prefix.length() == rhs.prefix.length() &&
          lhs.postfix.length() == rhs.postfix.length() &&
          lhs.num != -1 && rhs.num != -1) {
        int cmp = lhs.prefix.compareTo(rhs.prefix);
        if (cmp != 0)
          return cmp;
        if (lhs.num != rhs.num)
          return lhs.num - rhs.num;
        return lhs.postfix.compareTo(rhs.postfix);
      }
      return lhs.name.compareTo(rhs.name);
    }
  }

  /** Context for managing nested expressions. */
  public static class Frame {
    /** Last operator seen was '-' */
    public boolean subtract;
    /** Set of host names */
    public TreeSet<ClusterHostname> hosts = new TreeSet<ClusterHostname>(new ClusterHostnameComparator());
  };

  /**
   * Input stream token.
   * Abstraction for input stream token to facilitate host specification
   * parsing.  A host pattern is represented by a #value of 0 and will have
   * #hosts populated with the expansion of the pattern.
   */
  public static class Token {
    /** Token value (one of: 0 '(' '(' '+' '-' ',') */
    char value;
    /** Set of hosts represented by host pattern token */
    public TreeSet<ClusterHostname> hosts = new TreeSet<ClusterHostname>(new ClusterHostnameComparator());
  }

  public static class Range {
    int begin;
    int end;
  }

  /** Tokenizes a host specification pattern. */
  class Tokenizer {

    /**
     * Constructor.
     * Sets #m_spec to spec.
     * @param spec Host specification pattern
     */
    public Tokenizer(String spec) {
      m_spec = spec;
    }

    /**
     * Gets the next token.
     * @param token Reference to token
     * @returns <i>true</i> if <code>token</code> filled in with valid token,
     * <i>false</i> on EOS
     * @throws Exception with code set to Error::BAD_FORMAT on bad input
     */
    public boolean next(Token token) throws ParseException {

      skip_whitespace();

      if (m_pos == m_spec.length())
        return false;

      if (m_spec.charAt(m_pos) == '(' ||
          m_spec.charAt(m_pos) == ')' ||
          m_spec.charAt(m_pos) == '+' ||
          m_spec.charAt(m_pos) == ',' ||
          m_spec.charAt(m_pos) == '-') {
        token.value = m_spec.charAt(m_pos);
        m_pos++;
        return true;
      }

      if (!Character.isLetterOrDigit(m_spec.charAt(m_pos)) && m_spec.charAt(m_pos) == ']')
        throw new ParseException("Invalid character ']' encountered", current_position());

      String prefix = "";
      String host_pattern = "";
      int postfix_len = 0;
      Range range = new Range();

      if (Character.isLetterOrDigit(m_spec.charAt(m_pos))) {
        int base = m_pos;
        while (m_pos < m_spec.length() &&
               (Character.isLetterOrDigit(m_spec.charAt(m_pos)) ||
                m_spec.charAt(m_pos) == '.' ||
                m_spec.charAt(m_pos) == '-'))
          m_pos++;
        prefix = m_spec.substring(base, m_pos);
      }

      if (m_pos < m_spec.length() && m_spec.charAt(m_pos) == '[') {
        host_pattern = prefix;
        host_pattern += extract_numeric_range(range);
        if (m_pos < m_spec.length() &&
            (Character.isLetterOrDigit(m_spec.charAt(m_pos)) ||
             m_spec.charAt(m_pos) == '.' ||
             m_spec.charAt(m_pos) == '-')) {
          int base = m_pos;
          while (m_pos < m_spec.length() &&
                 (Character.isLetterOrDigit(m_spec.charAt(m_pos)) ||
                  m_spec.charAt(m_pos) == '.' ||
                  m_spec.charAt(m_pos) == '-'))
            m_pos++;
          postfix_len = m_pos - base;
          host_pattern += m_spec.substring(base, m_pos);
        }
      }

      if (m_pos < m_spec.length() && !Character.isWhitespace(m_spec.charAt(m_pos)) &&
          m_spec.charAt(m_pos) != '(' && m_spec.charAt(m_pos) != ')' &&
          m_spec.charAt(m_pos) != '+' && m_spec.charAt(m_pos) != ',' &&
          m_spec.charAt(m_pos) != '-')
        throw new ParseException("Invalid character '" + m_spec.charAt(m_pos) +
                                  "' encountered", current_position());

      token.hosts.clear();
      token.value = 0;

      if (!host_pattern.isEmpty()) {
        int i = range.begin;
        while (i <= range.end) {
          String name = String.format(host_pattern, i);
          String postfix = name.substring(name.length() - postfix_len);
          token.hosts.add( new ClusterHostname(name, prefix, i, postfix) );
          i++;
        }
      }
      else
        token.hosts.add(new ClusterHostname(prefix));

      return true;
    }


    /**
     * Gets character offset of current position.
     * @return Offset from beginning of spec of current position.
     */
    public int current_position() {
      return m_pos + 1;
    }

    /**
     * Skips over whitespace.
     * Advances #m_ptr to next non-whitespace character
     */
    private void skip_whitespace() {
      while (m_pos < m_spec.length() && Character.isWhitespace(m_spec.charAt(m_pos)))
        m_pos++;
    }

    /*
     * Saves current position.
     * Sets #m_saved_ptr to current value of #m_ptr.
     */
    private void save_position() { m_saved_pos = m_pos; }

    /**
     * Gets character offset of saved position.
     * @return Offset from beginning of spec of saved position.
     */
    private int saved_position() {
      return m_saved_pos + 1;
    }

    String extract_numeric_range(Range range) throws ParseException {

      save_position();

      assert m_spec.charAt(m_pos) == '[';

      m_pos++;

      skip_whitespace();

      // sanity check to make sure the next thing we see is a number
      if (m_pos == m_spec.length())
        throw new ParseException("Truncated range pattern", current_position());
      else if (m_spec.charAt(m_pos) < '0' && m_spec.charAt(m_pos) > '9')
        throw new ParseException("Invalid character '" + m_spec.charAt(m_pos) + "' in range pattern",
                                 current_position());

      boolean range_begin_leading_zero = m_spec.charAt(m_pos) == '0';

      // skip over begin number
      int base = m_pos;
      while (m_pos < m_spec.length() && Character.isDigit(m_spec.charAt(m_pos)))
        m_pos++;
      int range_begin_width = m_pos - base;
      range.begin = Integer.parseInt(m_spec.substring(base, m_pos));

      skip_whitespace();

      // Verify the range contains '-' separation character
      if (m_pos == m_spec.length())
        throw new ParseException("Truncated range pattern", current_position());
      if (m_spec.charAt(m_pos) != '-')
        throw new ParseException("Invalid character '" + m_spec.charAt(m_pos) + "' in range pattern",
                                 current_position());
                  
      m_pos++;

      skip_whitespace();

      // sanity check to make sure the next thing we see is a number
      if (m_pos == m_spec.length())
        throw new ParseException("Truncated range pattern", current_position());
      else if (!Character.isDigit(m_spec.charAt(m_pos)))
        throw new ParseException("Invalid character '" + m_spec.charAt(m_pos) + "' in range pattern",
                                 current_position());

      boolean range_end_leading_zero = m_spec.charAt(m_pos) == '0';

      // skip over end number
      base = m_pos;
      while (m_pos < m_spec.length() && Character.isDigit(m_spec.charAt(m_pos)))
        m_pos++;
      int range_end_width = m_pos - base;
      range.end = Integer.parseInt(m_spec.substring(base, m_pos));

      skip_whitespace();

      if (m_pos == m_spec.length())
        throw new ParseException("Truncated range pattern", current_position());
      else if (m_spec.charAt(m_pos) != ']')
        throw new ParseException("Invalid character '" + m_spec.charAt(m_pos) + "' in range pattern",
                                 current_position());
      else if (range_begin_width != range_end_width &&
               (range_begin_leading_zero || range_end_leading_zero))
        throw new ParseException("Fixed-width numeric range specifiers must be of equal length", current_position());

      m_pos++;

      if (range_begin_width != range_end_width)
        return "%d";

      return "%0" + range_begin_width + "d";
    }

    /** Host specification */
    private String m_spec;
    /** Current position within #m_spec */
    private int m_pos;
    /** Saved position within #m_spec */
    private int m_saved_pos;
  }

  public HostSpecification(String spec) {
    m_spec = spec;
  }

  public Vector<String> expand() throws ParseException {
    char last_token = (char)255;
    Stack<Frame> frame_stack = new Stack<Frame>();

    frame_stack.push(new Frame());

    Token token = new Token();
    Tokenizer tokenizer = new Tokenizer(m_spec);

    while (tokenizer.next(token)) {
      if (token.value == 0) {
        if (frame_stack.peek().subtract) {
          for (ClusterHostname host : token.hosts)
            frame_stack.peek().hosts.remove(host);
        }
        else {
          for (ClusterHostname host : token.hosts)
            frame_stack.peek().hosts.add(host);
        }
      }
      else if (token.value == '(')
        frame_stack.push(new Frame());
      else if (token.value == ')') {
        if (last_token == '+' || last_token == ',' || last_token == '-')
          throw new ParseException("Missing operand for '" + last_token + "'",
                                   tokenizer.current_position() - 1);
        if (frame_stack.size() == 1)
          throw new ParseException("Mis-matched parenthesis", tokenizer.current_position() - 1);

        Frame top = frame_stack.peek();
        frame_stack.pop();

        if (frame_stack.peek().subtract) {
          for (ClusterHostname host : top.hosts)
            frame_stack.peek().hosts.remove(host);
        }
        else {
          for (ClusterHostname host : top.hosts)
            frame_stack.peek().hosts.add(host);
        }
      }
      else if (token.value == '+' || token.value == ',') {
        if (last_token != 0 && last_token != ')')
          throw new ParseException("Missing operand for '" + token.value + "'",
                                   tokenizer.current_position() - 1);
        frame_stack.peek().subtract = false;
      }
      else if (token.value == '-') {
        if (last_token != 0 && last_token != ')')
          throw new ParseException("Missing operand for '-'",
                                   tokenizer.current_position() - 1);
        frame_stack.peek().subtract = true;
      }
      else
        throw new ParseException("Unrecognized character '" + token.value + "'",
                                 tokenizer.current_position() - 1);
      last_token = token.value;
    }

    if (frame_stack.size() != 1)
      throw new ParseException("Mis-matched parenthesis", tokenizer.current_position() - 1);

    if (last_token == '+' || last_token == ',' || last_token == '-')
      throw new ParseException("Missing operand for '" + last_token + "'",
                               tokenizer.current_position() - 1);

    Vector<String> expanded_hosts = new Vector<String>(frame_stack.peek().hosts.size());

    for (ClusterHostname ch : frame_stack.peek().hosts)
      expanded_hosts.add(ch.name);

    return expanded_hosts;
  }

  /** Host specification */
  private String m_spec;

}
