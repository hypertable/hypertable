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

#include <Common/Compat.h>

#include "DispatchHandler.h"
#include "Comm.h"
#include "Event.h"

#include <AsyncComm/ApplicationHandler.h>
#include <AsyncComm/ApplicationQueue.h>
#include <AsyncComm/ConnectionHandlerFactory.h>
#include <AsyncComm/ReactorFactory.h>

#include <Common/Init.h>
#include <Common/Error.h>
#include <Common/InetAddr.h>
#include <Common/System.h>
#include <Common/SockAddrMap.h>
#include <Common/Usage.h>

#include <chrono>
#include <iostream>
#include <queue>
#include <string>
#include <thread>

extern "C" {
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
}

using namespace Hypertable;
using namespace std;

namespace {

  int  g_delay = 0;
  bool g_verbose = false;
  const int DEFAULT_PORT = 11255;
  const char *usage[] = {
    "usage: sampleServer [OPTIONS]",
    "",
    "OPTIONS:",
    "  --connect-to=<addr>  Connect to a client listening on <addr> (TCP only)."
    "  --help          Display this help text and exit",
    "  --port=<n>      Specifies the port to listen on (default=11255)",
    "  --app-queue     Use an application queue for handling requests",
    "  --reactors=<n>  Specifies the number of reactors (default=1)",
    "  --delay=<ms>    Milliseconds to wait before echoing message (default=0)",
    "  --udp           Operate in UDP mode instead of TCP",
    "  --verbose,-v    Generate verbose output",
    ""
    "This is a sample program to test the AsyncComm library.  It establishes",
    "a connection with the sampleServer and sends each line of the input file",
    "to the server.  Each reply from the server is echoed to stdout.",
    (const char *)0
  };

  /**
   * This is the request handler class that is used when the server
   * is run with an application queue (--app-queue).  It just echos
   * back the message
   */
  class RequestHandler : public ApplicationHandler {
  public:

    RequestHandler(Comm *comm, EventPtr &event_ptr)
      : ApplicationHandler(event_ptr), m_comm(comm) { return; }

    virtual void run() {
      CommHeader header;
      header.initialize_from_request_header(m_event->header);
      CommBufPtr cbp(new CommBuf(header, m_event->payload_len));
      cbp->append_bytes((uint8_t *)m_event->payload,
                        m_event->payload_len);
      int error = m_comm->send_response(m_event->addr, cbp);
      if (error != Error::OK) {
        HT_ERRORF("Comm::send_response returned %s", Error::get_text(error));
      }
    }
  private:
    Comm *m_comm;
  };


  /**
   * This is the dispatch handler that is used when
   * the server is in TCP mode (default).
   */
  class Dispatcher : public DispatchHandler {

  public:

    Dispatcher(Comm *comm, ApplicationQueue *app_queue)
      : m_comm(comm), m_app_queue(app_queue) { return; }

    virtual void handle(EventPtr &event_ptr) {
      if (g_verbose && event_ptr->type == Event::CONNECTION_ESTABLISHED) {
        HT_INFO("Connection Established.");
      }
      else if (g_verbose && event_ptr->type == Event::DISCONNECT) {
        if (event_ptr->error != 0) {
          HT_INFOF("Disconnect : %s", Error::get_text(event_ptr->error));
        }
        else {
          HT_INFO("Disconnect.");
        }
      }
      else if (event_ptr->type == Event::ERROR) {
        HT_WARNF("Error : %s", Error::get_text(event_ptr->error));
      }
      else if (event_ptr->type == Event::MESSAGE) {
        if (m_app_queue == 0) {
          CommHeader header;
          header.initialize_from_request_header(event_ptr->header);
          CommBufPtr cbp(new CommBuf(header, event_ptr->payload_len));
          cbp->append_bytes((uint8_t *)event_ptr->payload,
                            event_ptr->payload_len);
          if (g_delay > 0)
            this_thread::sleep_for(chrono::milliseconds(g_delay));
          int error = m_comm->send_response(event_ptr->addr, cbp);
          if (error != Error::OK) {
            HT_ERRORF("Comm::send_response returned %s",
                      Error::get_text(error));
          }
        }
        else
          m_app_queue->add(new RequestHandler(m_comm, event_ptr));
      }
    }

  private:
    Comm             *m_comm;
    ApplicationQueue *m_app_queue;
  };



  /**
   * This is the dispatch handler that is used when
   * the server is in UDP mode.
   */
  class UdpDispatcher : public DispatchHandler {
  public:

    UdpDispatcher(Comm *comm) : m_comm(comm) { return; }

    virtual void handle(EventPtr &event_ptr) {
      if (event_ptr->type == Event::MESSAGE) {
        CommHeader header;
        header.initialize_from_request_header(event_ptr->header);
        CommBufPtr cbp(new CommBuf(header, event_ptr->payload_len));
        cbp->append_bytes((uint8_t *)event_ptr->payload,
                          event_ptr->payload_len);
        if (g_delay > 0)
          this_thread::sleep_for(chrono::milliseconds(g_delay));
        int error = m_comm->send_datagram(event_ptr->addr,
                                          event_ptr->local_addr, cbp);
        if (error != Error::OK) {
          HT_ERRORF("Comm::send_response returned %s", Error::get_text(error));
        }
      }
      else {
        HT_ERRORF("Error : %s", event_ptr->to_str().c_str());
      }
    }

  private:
    Comm           *m_comm;
  };


  /**
   * This handler factory gets passed into Comm::listen.  It
   * gets constructed with a pointer to a DispatchHandler.
   */
  class HandlerFactory : public ConnectionHandlerFactory {
  public:
    HandlerFactory(DispatchHandlerPtr &dhp)
      : m_dispatch_handler_ptr(dhp) { return; }

    virtual void get_instance(DispatchHandlerPtr &dhp) {
      dhp = m_dispatch_handler_ptr;
    }

  private:
    DispatchHandlerPtr m_dispatch_handler_ptr;
  };

}


/**
 * main function
 */
int main(int argc, char **argv) {
  Comm *comm;
  int rval, error;
  uint16_t port = DEFAULT_PORT;
  int reactor_count = 2;
  ApplicationQueue *app_queue = 0;
  bool udp = false;
  DispatchHandlerPtr dhp;
  CommAddress local_addr;
  InetAddr client_addr;

  Config::init(0, 0);

  memset(reinterpret_cast<void*>(&client_addr), 0, sizeof(client_addr));

  for (int i=1; i<argc; i++) {
    if (!strcmp(argv[i], "--help"))
      Usage::dump_and_exit(usage);
    else if (!strcmp(argv[i], "--app-queue")) {
      app_queue = new ApplicationQueue(5);
    }
    else if (!strncmp(argv[i], "--connect-to=", 13)) {
      if (!InetAddr::initialize(&client_addr, &argv[i][13]))
        HT_ABORT;
    }
    else if (!strncmp(argv[i], "--port=", 7)) {
      rval = atoi(&argv[i][7]);
      if (rval <= 1024 || rval > 65535) {
        cerr << "Invalid port.  Must be in the range of 1024-65535." << endl;
        exit(EXIT_FAILURE);
      }
      port = (uint16_t)rval;
    }
    else if (!strncmp(argv[i], "--reactors=", 11))
      reactor_count = atoi(&argv[i][11]);
    else if (!strncmp(argv[i], "--delay=", 8))
      g_delay = atoi(&argv[i][8]);
    else if (!strcmp(argv[i], "--udp"))
      udp = true;
    else if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v"))
      g_verbose = true;
    else
      Usage::dump_and_exit(usage);
  }

  try {

    ReactorFactory::initialize(reactor_count);

    comm = Comm::instance();

    if (g_verbose) {
      cout << "Listening on port " << port << endl;
      if (g_delay)
	cout << "Delay = " << g_delay << endl;
    }

    {
      InetAddr addr;
      InetAddr::initialize(&addr, "localhost", port);
      local_addr = addr;
    }

    if (!udp) {
      dhp = std::make_shared<Dispatcher>(comm, app_queue);

      if (client_addr.sin_port != 0) {
	if ((error = comm->connect(client_addr, local_addr, dhp)) != Error::OK) {
	  HT_ERRORF("Comm::connect error - %s", Error::get_text(error));
	  exit(EXIT_FAILURE);
	}
      }
      else {
        ConnectionHandlerFactoryPtr handler_factory = make_shared<HandlerFactory>(dhp);
	comm->listen(local_addr, handler_factory, dhp);
      }
    }
    else {
      assert(client_addr.sin_port == 0);
      dhp = std::make_shared<UdpDispatcher>(comm);
      comm->create_datagram_receive_socket(local_addr, 0, dhp);
    }

    ReactorFactory::join();

  }
  catch (Hypertable::Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    _exit(EXIT_FAILURE);
  }

  _exit(EXIT_SUCCESS);
}
