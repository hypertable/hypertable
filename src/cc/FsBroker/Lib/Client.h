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
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
* 02110-1301, USA.
*/

/// @file
/// Declarations for Client.
/// This file contains declarations for Client, a client proxy class for the
/// file system broker.

#ifndef FsBroker_Lib_Client_h
#define FsBroker_Lib_Client_h

#include <FsBroker/Lib/ClientBufferedReaderHandler.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>
#include <AsyncComm/DispatchHandlerSynchronizer.h>

#include <Common/Filesystem.h>
#include <Common/InetAddr.h>
#include <Common/Properties.h>
#include <Common/Status.h>

#include <memory>
#include <mutex>
#include <unordered_map>

namespace Hypertable {
	namespace FsBroker {
		namespace Lib {

			/// @addtogroup FsBrokerLib
			/// @{

			/** Proxy class for FS broker.  As specified in the general contract for a
			* Filesystem, commands that operate on the same file descriptor are
			* serialized by the underlying filesystem.  In other words, if you issue
			* three asynchronous commands, they will get carried out and their
			* responses will come back in the same order in which they were issued.
			*/
			class Client : public Filesystem {
			public:

				virtual ~Client();

				/** Constructor with explicit values.  Connects to the FS broker at the
				* address given by the addr argument and uses the timeout argument for
				* the request timeout values.
				*
				* @param conn_manager_ptr smart pointer to connection manager
				* @param addr address of FS broker to connect to
				* @param timeout_ms timeout value in milliseconds to use in requests
				*/
				Client(ConnectionManagerPtr &conn_manager_ptr,
					const sockaddr_in &addr, uint32_t timeout_ms);

				/** Constructor with config var map.  The following properties are read
				* to determine the location of the broker and the request timeout value:
				* <pre>
				* FsBroker.port
				* FsBroker.host
				* FsBroker.timeout
				* </pre>
				*
				* @param conn_manager_ptr smart pointer to connection manager
				* @param cfg config variables map
				*/
				Client(ConnectionManagerPtr &conn_manager_ptr, PropertiesPtr &cfg);

				/** Constructor without connection manager.
				*
				* @param comm pointer to the Comm object
				* @param addr remote address of already connected FsBroker
				* @param timeout_ms timeout value in milliseconds to use in requests
				*/
				Client(Comm *comm, const sockaddr_in &addr, uint32_t timeout_ms);

				/** Convenient contructor for dfs testing
				*
				* @param host - dfs hostname
				* @param port - dfs port
				* @param timeout_ms - timeout (in milliseconds) for requests
				*/
				Client(const String &host, int port, uint32_t timeout_ms);

				/** Waits up to max_wait_secs for a connection to be established with the
				* FS broker.
				*
				* @param max_wait_ms maximum amount of time to wait @return true if
				* connected, false otherwise
				*/
				bool wait_for_connection(uint32_t max_wait_ms) {
					if (m_conn_mgr)
						return m_conn_mgr->wait_for_connection(m_addr, max_wait_ms);
					return true;
				}

				void open(const String &name, uint32_t flags, DispatchHandler *handler) override;
				int open(const String &name, uint32_t flags) override;
				int open_buffered(const String &name, uint32_t flags, uint32_t buf_size,
					uint32_t outstanding, uint64_t start_offset = 0,
					uint64_t end_offset = 0) override;
				void decode_response_open(EventPtr &event, int32_t *fd) override;

				void create(const String &name, uint32_t flags,
					int32_t bufsz, int32_t replication,
					int64_t blksz, DispatchHandler *handler) override;
				int create(const String &name, uint32_t flags, int32_t bufsz,
					int32_t replication, int64_t blksz) override;
				void decode_response_create(EventPtr &event, int32_t *fd) override;

				void close(int32_t fd, DispatchHandler *handler) override;
				void close(int32_t fd) override;

				void read(int32_t fd, size_t amount, DispatchHandler *handler) override;
				size_t read(int32_t fd, void *dst, size_t amount) override;
				void decode_response_read(EventPtr &event, const void **buffer,
					uint64_t *offset, uint32_t *length) override;

				void append(int32_t fd, StaticBuffer &buffer, Flags flags,
					DispatchHandler *handler) override;
				size_t append(int32_t fd, StaticBuffer &buffer,
					Flags flags = Flags::NONE) override;
				void decode_response_append(EventPtr &event, uint64_t *offset,
					uint32_t *length) override;

				void seek(int32_t fd, uint64_t offset, DispatchHandler *handler) override;
				void seek(int32_t fd, uint64_t offset) override;

				void remove(const String &name, DispatchHandler *handler) override;
				void remove(const String &name, bool force = true) override;

				void length(const String &name, bool accurate,
					DispatchHandler *handler) override;
				int64_t length(const String &name, bool accurate = true) override;
				int64_t decode_response_length(EventPtr &event) override;

				void pread(int32_t fd, size_t len, uint64_t offset,
					bool verify_checksum, DispatchHandler *handler) override;
				size_t pread(int32_t fd, void *dst, size_t len, uint64_t offset,
					bool verify_checksum) override;
				void decode_response_pread(EventPtr &event, const void **buffer,
					uint64_t *offset, uint32_t *length) override;

				void mkdirs(const String &name, DispatchHandler *handler) override;
				void mkdirs(const String &name) override;

				void flush(int32_t fd, DispatchHandler *handler) override;
				void flush(int32_t fd) override;

				void sync(int32_t fd) override;

				void rmdir(const String &name, DispatchHandler *handler) override;
				void rmdir(const String &name, bool force = true) override;

				void readdir(const String &name, DispatchHandler *handler) override;
				void readdir(const String &name, std::vector<Dirent> &listing) override;
				void decode_response_readdir(EventPtr &event,
					std::vector<Dirent> &listing) override;

				void exists(const String &name, DispatchHandler *handler) override;
				bool exists(const String &name) override;
				bool decode_response_exists(EventPtr &event) override;

				void rename(const String &src, const String &dst,
					DispatchHandler *handler) override;
				void rename(const String &src, const String &dst) override;

				void status(Status &status, Timer *timer = 0) override;
				void decode_response_status(EventPtr &event, Status &status) override;

				void debug(int32_t command, StaticBuffer &serialized_parameters) override;
				void debug(int32_t command, StaticBuffer &serialized_parameters,
					DispatchHandler *handler) override;

				enum {
					/// Perform immediate shutdown
					SHUTDOWN_FLAG_IMMEDIATE = 0x01
				};

				/** Shuts down the FS broker.  Issues a shutdown command to the FS
				* broker.  If the flag is set to SHUTDOWN_FLAG_IMMEDIATE, then
				* the broker will call exit(0) directly from the I/O reactor thread.
				* Otherwise, a shutdown command will get added to the broker's
				* application queue, allowing the shutdown to be handled more
				* gracefully.
				*
				* @param flags controls how broker gets shut down
				* @param handler response handler
				*/
				void shutdown(uint16_t flags, DispatchHandler *handler);

				/** Gets the configured request timeout value.
				*
				* @return timeout value in milliseconds
				*/
				uint32_t get_timeout() { return m_timeout_ms; }

			private:

				/// Sends a message to the FS broker.
				/// @param cbuf message to send
				/// @param handler response handler
				/// @param timer Deadline timer
				void send_message(CommBufPtr &cbuf, DispatchHandler *handler, Timer *timer = 0);

				std::mutex m_mutex;
				Comm *m_comm;
				ConnectionManagerPtr m_conn_mgr;
				InetAddr m_addr;
				uint32_t m_timeout_ms;
				std::unordered_map<uint32_t, ClientBufferedReaderHandler *> m_buffered_reader_map;
			};

			/// Smart pointer to Client
			typedef std::shared_ptr<Client> ClientPtr;

			/// @}

		}
	}
}


#endif // FsBroker_Lib_Client_h
