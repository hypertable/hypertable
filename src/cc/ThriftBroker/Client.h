/* -*- c++ -*-
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
* along with Hypertable. If not, see <http://www.gnu.org/licenses/>
*/

#ifndef Hypertable_ThriftBroker_Client_h
#define Hypertable_ThriftBroker_Client_h

// Note: do NOT add any hypertable dependencies in this file

#include <transport/TSocket.h>
#include <transport/TTransportUtils.h>
#include <transport/TZlibTransport.h>
#include <protocol/TBinaryProtocol.h>

#include "gen-cpp/HqlService.h"

#include <memory>

namespace Hypertable {
	namespace Thrift {

		using namespace apache::thrift;
		using namespace apache::thrift::protocol;
		using namespace apache::thrift::transport;

		// Client Transport Choices		
		// in case name "Transport" collide with apache::thrift ns 
		// HyperThriftTransport can be a good choice
		enum Transport
		{
			FRAMED = 1,
			ZLIB = 2,
		};


		// helper to initialize base class of Client
		struct ClientHelper {
			boost::shared_ptr<TSocket> socket;
			boost::shared_ptr<TTransport> transport;
			boost::shared_ptr<TProtocol> protocol;

			// Thrift client transport selector
			inline TTransport* select_transport(Transport ttp){
				switch (ttp) {
				case Transport::ZLIB:
					return new TZlibTransport(socket);
				default:
					return new TFramedTransport(socket);
				}
			}

			ClientHelper(Transport ttp, const std::string &host, int port, int timeout_ms) :
				socket(new TSocket(host, port)), 
				transport(static_cast<boost::shared_ptr<TTransport>>(select_transport(ttp))),
				protocol(new TBinaryProtocol(transport)) {

				socket->setConnTimeout(timeout_ms);
				socket->setSendTimeout(timeout_ms);
				socket->setRecvTimeout(timeout_ms);
			};
		};


		/**
		* A client for the ThriftBroker.
		*/
		class Client : private ClientHelper, public ThriftGen::HqlServiceClient {
		public:
			Client(const std::string &host, int port, int timeout_ms = 300000,
				bool open = true)
				: ClientHelper(Transport::FRAMED, host, port, timeout_ms), HqlServiceClient(protocol),
				m_do_close(false) {

				if (open) {
					transport->open();
					m_do_close = true;
				}
			}
			Client(Transport ttp, const std::string &host, int port, int timeout_ms = 300000,
				bool open = true)
				: ClientHelper(ttp, host, port, timeout_ms), HqlServiceClient(protocol),
				m_do_close(false) {

				if (open) {
					transport->open();
					m_do_close = true;
				}
			}

			virtual ~Client() {
				if (m_do_close) {
					transport->close();
					m_do_close = false;
				}
			}
		private:
			bool m_do_close;
		};

		/// Smart pointer to client
		typedef std::shared_ptr<Client> ClientPtr;


	}
} // namespace Hypertable::Thrift

#endif // Hypertable_ThriftBroker_Client_h