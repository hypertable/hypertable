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

package org.hypertable.examples.PerformanceTest;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.BufferedReader;
import java.nio.ByteOrder;
import java.nio.channels.SelectionKey;
import java.util.LinkedList;
import java.util.logging.Logger;

import org.hypertable.Common.Error;
import org.hypertable.Common.HypertableException;
import org.hypertable.Common.Usage;

import org.hypertable.AsyncComm.Comm;
import org.hypertable.AsyncComm.CommBuf;
import org.hypertable.AsyncComm.CommHeader;
import org.hypertable.AsyncComm.DispatchHandler;
import org.hypertable.AsyncComm.Event;
import org.hypertable.AsyncComm.ReactorFactory;
import org.hypertable.Common.Serialization;

public class Client {

  static final Logger log = Logger.getLogger("org.hypertable");

  static String usage[] = {
    "",
    "usage: Client [OPTIONS] <host>[:<port>]",
    "",
    "OPTIONS:",
    "  --thriftbroker-port=<n>  Specifies the ThriftBroker port",
    "  --timeout=<t>            Specifies the connection timeout value",
    "  --reactors=<n>           Specifies the number of reactors",
    "",
    "This program is part of a performance test suite.  It acts as a single client",
    "supplying load to a Bigtable cluster and gets work from a dispatcher process.",
    "The dispatcher process is identified by the <host> and <port> arguments given",
    "on the command line",
    "",
    null
  };

  static class ResponseHandler implements DispatchHandler {

    static enum State { IDLE, CONNECTED, DISCONNECTED }

    public ResponseHandler() {
      mState = State.IDLE;
    }

    public void clearState() {
      mState = State.IDLE;
    }

    public void handle(Event event) {
      synchronized (this) {

        if (event.type == Event.Type.CONNECTION_ESTABLISHED) {
          System.out.println("Connection Established.");
          mState = State.CONNECTED;
          notify();
        }
        else if (event.type == Event.Type.DISCONNECT) {
          if (event.error != Error.COMM_CONNECT_ERROR) {
            System.out.println("Disconnect.  " + Error.GetText(event.error));
            System.exit(0);
          }
          mState = State.DISCONNECTED;
          notify();
        }
        else if (event.type == Event.Type.ERROR) {
          System.err.println("Error : " + Error.GetText(event.error));
          //exit(1);
        }
        else if (event.type == Event.Type.MESSAGE) {
          Event newEvent = new Event(event);
          mQueue.offer(newEvent);
          notify();
        }
      }
    }

    public synchronized boolean WaitForConnection()
      throws InterruptedException {
      while (mState == State.IDLE)
        wait();
      return mState == State.CONNECTED;
    }

    public synchronized Event GetResponse() throws InterruptedException {
      while (mQueue.isEmpty()) {
        wait();
        if (mState != State.CONNECTED)
          return null;
      }
      return mQueue.remove();
    }

    private LinkedList<Event> mQueue = new LinkedList<Event>();
    private State mState;
  }

  private static String getPid() throws IOException {
    byte[] bo = new byte[100];
    String[] cmd = {"bash", "-c", "echo $PPID"};
    Process p = Runtime.getRuntime().exec(cmd);
    p.getInputStream().read(bo);
    return new String(bo);
  }

  static boolean msShutdown = false;
  static final int DEFAULT_PORT = 11256;

  public static void main(String [] args)
    throws InterruptedException, IOException {
    Integer waitObj = new Integer(0);
    int port = DEFAULT_PORT;
    int thriftbroker_port = DriverHypertable.DEFAULT_THRIFTBROKER_PORT;
    String host = null;
    File pidfile = null;
    long timeout = 0;

    if (args.length == 0) {
      for (int i = 0; usage[i] != null; i++)
        System.out.println(usage[i]);
      System.exit(0);
    }

    for (int i=0; i<args.length; i++) {
      if (args[i].startsWith("--timeout="))
        timeout = Integer.parseInt(args[i].substring(10));
      else if (args[i].startsWith("--thriftbroker-port="))
        thriftbroker_port = Integer.parseInt(args[i].substring(20));
      else if (args[i].equals("--pidfile")) {
        pidfile = new File(args[++i]);
      }
      else if (host == null) {
        int colon = args[i].indexOf(':');
        if (colon == -1)
          host = args[i];
        else {
          host = args[i].substring(0, colon);
          port = Integer.parseInt(args[i].substring(colon+1));
        }
      }
      else
        Usage.DumpAndExit(usage);
    }

    System.out.println("Connecting to " + host + ":" + port);

    if (pidfile != null) {
      if (!pidfile.exists()) {
        FileWriter pidFileWriter = new FileWriter(pidfile);
        pidFileWriter.write(getPid());
        pidFileWriter.close();
      }
      pidfile.deleteOnExit();
    }

    try {
      Event event;
      Driver driver = null;
      int error;
      CommHeader header = new CommHeader();
      Message message, messageReady = new Message(Message.Type.READY);
      MessageRegister messageRegister = new MessageRegister();
      MessageSummary messageSummary;
      String clientName = null;

      ReactorFactory.Initialize((short)1);

      mDispatcherAddr = new InetSocketAddress(host, port);
      mComm = new Comm(0);
      mRespHandler = new ResponseHandler();

      while (true) {

        mRespHandler.clearState();

        if ((error = mComm.Connect(mDispatcherAddr, timeout, mRespHandler)) != Error.OK) {
          System.err.println("Connect error : " + Error.GetText(error));
          System.exit(1);
        }

        if (!mRespHandler.WaitForConnection()) {
          System.out.println("Unable to connect to test dispatcher, will try again in 3 seconds...");
          synchronized (waitObj) {
            waitObj.wait(3000);
          }
        }
        else
          break;
      }

      /**
       * Send REGISTER message
       */
      messageRegister.setHostName( InetAddress.getLocalHost().getHostName() );
      messageRegister.setHostAddress( InetAddress.getLocalHost().getHostAddress() );
      clientName = InetAddress.getLocalHost() + "-" + getPid();
      messageRegister.setClientName(clientName);
      CommBuf cbuf = messageRegister.createCommBuf(header);
      SendRequest(cbuf);

      /**
       * Receive SETUP message
       */
      if ((event = mRespHandler.GetResponse()) == null)
        throw new HypertableException(Error.COMM_CONNECT_ERROR, "Receiving SETUP message");
      message = MessageFactory.createMessage(event.payload);
      System.out.println(message);
      if (message.type() != Message.Type.SETUP)
        throw new HypertableException(Error.FAILED_EXPECTATION, "Expected SETUP message");

      Setup setup = ((MessageSetup)message).setup;

      DriverCommon.VALUE_DATA_FILE = setup.valueData;

      if (setup.driver.equals("hypertable")) {
        driver = new DriverHypertable(thriftbroker_port);
      }
      else if (setup.driver.equals("hbase"))
        driver = new DriverHBase();
      else {
        MessageError messageError = new MessageError("Unrecognized driver: " + setup.driver);
        cbuf = messageError.createCommBuf(header);
        SendRequest(cbuf);
        synchronized (waitObj) {
          waitObj.wait(5000);
        }
        ReactorFactory.Shutdown();
        System.exit(1);
      }

      driver.setup(setup);

      /**
       * Create and send READY message
       */
      cbuf = messageReady.createCommBuf(header);
      SendRequest(cbuf);

      while ((event = mRespHandler.GetResponse()) != null) {
        message = MessageFactory.createMessage(event.payload);
        if (message.type() == Message.Type.TASK) {
          driver.runTask(((MessageTask)message).task);
          System.out.println( message );
        }
        else if (message.type() == Message.Type.FINISHED) {
          driver.teardown();
          messageSummary = new MessageSummary(clientName, driver.getResult());
          cbuf = messageSummary.createCommBuf(header);
          SendRequest(cbuf);
          System.out.println( message );
          break;
        }
        else
          System.out.println( "error: message type " + message.type().ordinal() );

        cbuf = messageReady.createCommBuf(header);
        SendRequest(cbuf);
      }

      /**
       *  Wait 5 seconds and then exit
       */
      synchronized (waitObj) {
        waitObj.wait(5000);
      }

      ReactorFactory.Shutdown();
    }
    catch (Exception e) {
      e.printStackTrace();
      System.exit(1);
    }
  }

  static void SendRequest(CommBuf cbuf) throws HypertableException, InterruptedException {
    int retries = 0;
    int error;
    while ((error = mComm.SendRequest(mDispatcherAddr, cbuf, mRespHandler)) != Error.OK) {
      if (error == Error.COMM_NOT_CONNECTED) {
        if (retries == 5)
          throw new HypertableException(Error.COMM_CONNECT_ERROR, "timeout");
        Integer waitObj = new Integer(0);
        synchronized (waitObj) {
          waitObj.wait(1000);
        }
        retries++;
      }
      else
        throw new HypertableException(error);
    }
  }

  static public InetSocketAddress mDispatcherAddr;
  static public Comm mComm;
  static public ResponseHandler mRespHandler;

}

