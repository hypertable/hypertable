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

package org.hypertable.FsBroker.Lib;

import java.net.ProtocolException;
import java.util.logging.Logger;
import org.hypertable.AsyncComm.ApplicationHandler;
import org.hypertable.AsyncComm.ApplicationQueue;
import org.hypertable.AsyncComm.Comm;
import org.hypertable.AsyncComm.DispatchHandler;
import org.hypertable.AsyncComm.Event;
import org.hypertable.AsyncComm.ResponseCallback;
import org.hypertable.Common.Error;

/**
 * This is the connection dispatch handler that gets registered with AsyncComm
 * for each incoming connection.  For MESSAGE events, it creates a
 * RequestHandler to carry out the request and enque's it on the application
 * work queue.  For DISCONNECT events, it purges all of the open file
 * descriptors that were created on the connection.  All other events are
 * logged.
 */
public class ConnectionHandler implements DispatchHandler {

  static final Logger log = Logger.getLogger("org.hypertable");

  public ConnectionHandler(Comm comm, ApplicationQueue appQueue,
                           Broker broker) {
    mComm = comm;
    mAppQueue = appQueue;
    mBroker = broker;
  }

  public void handle(Event event) {

    if (event.type == Event.Type.MESSAGE) {
      //log.info(event.toString());

      ApplicationHandler requestHandler;

      switch ((int)event.header.command) {
      case Protocol.COMMAND_OPEN:
        requestHandler = new RequestHandlerOpen(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_CLOSE:
        requestHandler = new RequestHandlerClose(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_CREATE:
        requestHandler = new RequestHandlerCreate(mComm, mBroker,
                                                  event);
        break;
      case Protocol.COMMAND_LENGTH:
        requestHandler = new RequestHandlerLength(mComm, mBroker,
                                                  event);
        break;
      case Protocol.COMMAND_READ:
        requestHandler = new RequestHandlerRead(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_APPEND:
        requestHandler = new RequestHandlerAppend(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_SEEK:
        requestHandler = new RequestHandlerSeek(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_REMOVE:
        requestHandler = new RequestHandlerRemove(mComm, mBroker,
                                                  event);
        break;
      case Protocol.COMMAND_PREAD:
        requestHandler = new RequestHandlerPositionRead(mComm, mBroker,
                                                        event);
        break;
      case Protocol.COMMAND_MKDIRS:
        requestHandler = new RequestHandlerMkdirs(mComm, mBroker,
                                                  event);
        break;
      case Protocol.COMMAND_SHUTDOWN:
        requestHandler = new RequestHandlerShutdown(mComm, mAppQueue, mBroker, event);
        break;
      case Protocol.COMMAND_STATUS:
        requestHandler = new RequestHandlerStatus(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_FLUSH:
        requestHandler = new RequestHandlerFlush(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_SYNC:
        requestHandler = new RequestHandlerSync(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_RMDIR:
        requestHandler = new RequestHandlerRmdir(mComm, mBroker, event);
        break;
      case Protocol.COMMAND_READDIR:
        requestHandler = new RequestHandlerReaddir(mComm, mBroker,
                                                   event);
        break;
      case Protocol.COMMAND_EXISTS:
        requestHandler = new RequestHandlerExists(mComm, mBroker,
                                                  event);
        break;
      case Protocol.COMMAND_RENAME:
        requestHandler = new RequestHandlerRename(mComm, mBroker,
                                                  event);
        break;
      default:
        ResponseCallback cb = new ResponseCallback(mComm, event);
        log.severe("Command code " + event.header.command
                   + " not implemented");
        cb.error(Error.PROTOCOL_ERROR, "Command code "
                 + event.header.command + " not implemented");
        return;
      }

      mAppQueue.Add(requestHandler);

    }
    else if (event.type == Event.Type.DISCONNECT) {
      log.info(event.toString() + " : Closing all open handles from "
               + event.addr);
      OpenFileMap ofMap = mBroker.GetOpenFileMap();
      ofMap.RemoveAll(event.addr);
    }
    else
      log.info(event.toString());
  }

  private Comm mComm;
  private ApplicationQueue mAppQueue;
  private Broker mBroker;
}

