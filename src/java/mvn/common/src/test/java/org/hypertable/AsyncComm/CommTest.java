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

import java.io.File;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.util.logging.Logger;

import org.junit.*;
import static org.junit.Assert.*;

public class CommTest {

    static final Logger log = Logger.getLogger("org.hypertable");

    static final int DEFAULT_PORT = 32998;
    static final String DEFAULT_PORT_ARG = "--port=32998";

    private static class ServerLauncher {

        private static class OutputRedirector implements Runnable {
            public OutputRedirector(Process proc) {
                mProc = proc;
            }
            public void run() {
                byte [] buf = new byte [ 128 ];
                int nread, rval = 0;
                InputStream istream = mProc.getInputStream();
                try {
                    while ((nread = istream.read(buf)) > 0)
                        System.out.print(new String(buf, 0, nread));
                    rval = mProc.waitFor();
                }
                catch (Exception e) {
                    e.printStackTrace();
                }
            }
            private Process mProc;
        }

        public ServerLauncher() {
            try {
                ProcessBuilder pbuilder = new ProcessBuilder("java",
                    "-classpath", System.getProperty("java.class.path"),
                    "org.hypertable.AsyncComm.SampleServer", DEFAULT_PORT_ARG);
                pbuilder.redirectErrorStream(true);
                mProc = pbuilder.start();
                Integer intObj = new Integer(0);
                synchronized (intObj) {
                    intObj.wait(2000);
                }

                mOutputRedirectorThread = new Thread(
                    new OutputRedirector(mProc), "CommTest Output Redirector");
                mOutputRedirectorThread.start();
            }
            catch (Exception e) {
                e.printStackTrace();
                msShutdown = true;
            }
        }
        public void Kill() {
            mProc.destroy();
        }
        private Process mProc;
        private Thread  mOutputRedirectorThread;
    };

    @Before public void setUp() {

        try {

            ReactorFactory.Initialize((short)1);

            slauncher = new ServerLauncher();

            mAddr = new InetSocketAddress("localhost", DEFAULT_PORT);

            mComm = new Comm(0);
            mConnectionManager = new ConnectionManager(mComm);
            mConnectionManager.Add(mAddr, 5, "SampleServer");

            if (!mConnectionManager.WaitForConnection(mAddr, 30)) {
                log.fine("Connect error");
                msShutdown = true;
                return;
            }

            (new File("/tmp/CommTest.output.1")).delete();
            (new File("/tmp/CommTest.output.2")).delete();

            CommTestThreadFunction threadFunc1 = new CommTestThreadFunction(
                mComm, mAddr, "/usr/share/dict/words");
            threadFunc1.SetOutputFile("/tmp/CommTest.output.1");
            mThread1 = new Thread(threadFunc1, "CommTest thread 1");
            mThread1.start();

            CommTestThreadFunction threadFunc2 = new CommTestThreadFunction(
                mComm, mAddr, "/usr/share/dict/words");
            threadFunc2.SetOutputFile("/tmp/CommTest.output.2");
            mThread2 = new Thread(threadFunc2, "CommTest thread 2");
            mThread2.start();
        }
        catch (Exception e) {
            e.printStackTrace();
            msShutdown = true;
        }
    }

    @Test public void testComm() {

        if (msShutdown)
            assertTrue(false);

        try {
            mThread1.join();
            mThread2.join();

            ProcessBuilder pbuilder;
            Process proc;
            InputStream istream;
            byte [] buf = new byte [ 128 ];
            int nread, rval = 0;

            pbuilder = new ProcessBuilder("diff", "/usr/share/dict/words",
                "/tmp/CommTest.output.1");
            pbuilder.redirectErrorStream(true);
            proc = pbuilder.start();
            istream = proc.getInputStream();
            while ((nread = istream.read(buf)) > 0)
                System.out.print(new String(buf, 0, nread));
            assertTrue(proc.waitFor() == 0);

            pbuilder = new ProcessBuilder("diff", "/tmp/CommTest.output.1",
                "/tmp/CommTest.output.2");
            pbuilder.redirectErrorStream(true);
            proc = pbuilder.start();
            istream = proc.getInputStream();
            while ((nread = istream.read(buf)) > 0)
                System.out.print(new String(buf, 0, nread));
            assertTrue(proc.waitFor() == 0);

        }
        catch (Exception e) {
            e.printStackTrace();
            assertTrue(false);
        }
    }

    @After public void tearDown() {
        slauncher.Kill();
    }

    public static junit.framework.Test suite() {
        return new junit.framework.JUnit4TestAdapter(CommTest.class);
    }

    public static void main(String args[]) {
        org.junit.runner.JUnitCore.main("org.hypertable.AsyncComm.CommTest");
    }

    private Comm mComm;
    private ConnectionManager mConnectionManager;
    private ServerLauncher    slauncher;
    private InetSocketAddress mAddr;
    protected static boolean  msShutdown = false;
    Thread mThread1;
    Thread mThread2;
}
