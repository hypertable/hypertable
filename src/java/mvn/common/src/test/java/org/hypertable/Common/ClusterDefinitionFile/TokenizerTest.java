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

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.IOException;
import java.lang.NumberFormatException;
import java.lang.System;
import java.net.InetSocketAddress;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.Charset;
import java.text.ParseException;
import java.util.Arrays;
import java.util.Vector;
import java.util.logging.Logger;

import org.junit.*;
import static org.junit.Assert.*;

public class TokenizerTest {

  static final Logger log = Logger.getLogger("org.hypertable");

  static private Vector<String> badInput;

    @Before public void setUp() {
      badInput = new Vector<String>(4);
      badInput.add("# comment\n\nrole master test00\n");
      badInput.add("# comment\n\nrole: master test00\ntask doit { echo \"done\" }");
      badInput.add("# comment\n\nrole: master test00\ninclude cluster.tasks\ntask: doit { echo \"done\" }");
      badInput.add("# comment\n\nrole: master test00\nincluding: cluster.tasks\ntask: doit { echo \"done\" }");
    }

  private void writeOutput(CharBuffer content, FileChannel fc) throws IOException, ParseException {
    content.flip();
    Tokenizer tokenizer = new Tokenizer("TokenizerTest", content);
    Token token;
    while ((token = tokenizer.next()) != null) {
      String str = "Token " + token.type() + "\n";
      fc.write(ByteBuffer.wrap(str.getBytes("UTF-8")));
      fc.write(ByteBuffer.wrap(token.text().getBytes("UTF-8")));
    }
  }

    @Test public void testTokenizer() {

        try {
          InputStream is = TokenizerTest.class.getClassLoader().getResourceAsStream("TokenizerTest.input");
          assert is != null;
          BufferedReader br = new BufferedReader(new InputStreamReader(is));
          CharBuffer content = CharBuffer.allocate(1024*1024);
          FileChannel fc = new FileOutputStream("TokenizerTest.output").getChannel();
          String line;
          while ((line = br.readLine()) != null) {
            if (line.startsWith("#### test-definition:")) {
              if (content.position() > 0) {
                writeOutput(content, fc);
                content.clear();
              }
            }
            content.put(line);
            content.put("\n");
          }
          if (content.position() > 0)
            writeOutput(content, fc);

          /* Diff output with golden file */
          URL url = TokenizerTest.class.getClassLoader().getResource("TokenizerTest.golden");
          assert url.getProtocol().equals("file");
          Process proc = new ProcessBuilder("diff", "TokenizerTest.output", url.getFile()).start();
          assertTrue(proc.waitFor() == 0);
        }
        catch (Exception e) {
            e.printStackTrace();
            assertTrue(false);
        }

        for (String spec : badInput) {
          try {
            CharBuffer buf = CharBuffer.allocate(spec.length());
            buf.put(spec);
            buf.flip();
            Tokenizer tokenizer = new Tokenizer("TokenizerTest", buf);
            Token token;
            while ((token = tokenizer.next()) != null)
              ;
            assertTrue(false);
          }
          catch (ParseException e) {
            System.out.println(e.getMessage() + " at " + e.getErrorOffset());
          }
          catch (IOException e) {
            System.out.println(e.getMessage());
          }
        }
    }

    @After public void tearDown() {
    }

    public static junit.framework.Test suite() {
      return new junit.framework.JUnit4TestAdapter(TokenizerTest.class);
    }

    public static void main(String args[]) {
      org.junit.runner.JUnitCore.main("org.hypertable.Common.ClusterDefinition.TokenizerTest");
    }

}
