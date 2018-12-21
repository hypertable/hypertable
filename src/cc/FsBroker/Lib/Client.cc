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

/// @file
/// Definitions for Client.
/// This file contains definitions for Client, a client proxy class for the
/// file system broker.

#include <Common/Compat.h>

#include "Client.h"

#include "Request/Handler/Factory.h"
#include "Request/Parameters/Append.h"
#include "Request/Parameters/Close.h"
#include "Request/Parameters/Create.h"
#include "Request/Parameters/Debug.h"
#include "Request/Parameters/Exists.h"
#include "Request/Parameters/Flush.h"
#include "Request/Parameters/Length.h"
#include "Request/Parameters/Mkdirs.h"
#include "Request/Parameters/Open.h"
#include "Request/Parameters/Pread.h"
#include "Request/Parameters/Readdir.h"
#include "Request/Parameters/Read.h"
#include "Request/Parameters/Remove.h"
#include "Request/Parameters/Rename.h"
#include "Request/Parameters/Rmdir.h"
#include "Request/Parameters/Seek.h"
#include "Request/Parameters/Shutdown.h"
#include "Request/Parameters/Sync.h"
#include "Response/Parameters/Append.h"
#include "Response/Parameters/Exists.h"
#include "Response/Parameters/Length.h"
#include "Response/Parameters/Open.h"
#include "Response/Parameters/Read.h"
#include "Response/Parameters/Readdir.h"
#include "Response/Parameters/Status.h"

#include <AsyncComm/Comm.h>
#include <AsyncComm/CommBuf.h>
#include <AsyncComm/CommHeader.h>
#include <AsyncComm/Protocol.h>

#include <Common/Error.h>
#include <Common/Filesystem.h>
#include <Common/Logger.h>
#include <Common/Serialization.h>

using namespace Hypertable;
using namespace Serialization;
using namespace Hypertable::FsBroker;
using namespace Hypertable::FsBroker::Lib;
using namespace std;

Client::Client(ConnectionManagerPtr &conn_mgr, const sockaddr_in &addr,
	uint32_t timeout_ms)
	: m_conn_mgr(conn_mgr), m_addr(addr), m_timeout_ms(timeout_ms) {
	m_comm = conn_mgr->get_comm();
	conn_mgr->add(m_addr, m_timeout_ms, "FS Broker");
}

Client::Client(ConnectionManagerPtr &conn_mgr, PropertiesPtr &props)
	: m_conn_mgr(conn_mgr) {
	m_comm = conn_mgr->get_comm();

	String host = props->get_str("FsBroker.Host");
	uint16_t port = props->get_i16("FsBroker.Port");

  m_timeout_ms = props->get_pref<int32_t>(
			{"FsBroker.Timeout", "Hypertable.Request.Timeout"});
	
	InetAddr::initialize(&m_addr, host.c_str(), port);

	conn_mgr->add(m_addr, m_timeout_ms, "FS Broker");
}

Client::Client(Comm *comm, const sockaddr_in &addr, uint32_t timeout_ms)
	: m_comm(comm), m_conn_mgr(0), m_addr(addr), m_timeout_ms(timeout_ms) {
}

Client::Client(const String &host, int port, uint32_t timeout_ms)
	: m_timeout_ms(timeout_ms) {
	InetAddr::initialize(&m_addr, host.c_str(), port);
	m_comm = Comm::instance();
	m_conn_mgr = make_shared<ConnectionManager>(m_comm);
	m_conn_mgr->add(m_addr, timeout_ms, "FS Broker");
	if (!m_conn_mgr->wait_for_connection(m_addr, timeout_ms))
		HT_THROW(Error::REQUEST_TIMEOUT,
			"Timed out waiting for connection to FS Broker");
}


Client::~Client() {
	/** this causes deadlock in RangeServer shutdown
	if (m_conn_mgr)
	m_conn_mgr->remove(m_addr);
	*/
}


void
Client::open(const String &name, uint32_t flags, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_OPEN);
	Request::Parameters::Open params(name, flags, 0);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, handler);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error opening FS file: %s", name.c_str());
	}
}


int
Client::open(const String &name, uint32_t flags) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_OPEN);
	Request::Parameters::Open params(name, flags, 0);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		int32_t fd;
		decode_response_open(event, &fd);
		return fd;
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error opening FS file: %s", name.c_str());
	}
}


int
Client::open_buffered(const String &name, uint32_t flags, uint32_t buf_size,
	uint32_t outstanding, uint64_t start_offset,
	uint64_t end_offset) {
	try {
		HT_ASSERT((flags & Filesystem::OPEN_FLAG_DIRECTIO) == 0 ||
			(HT_IO_ALIGNED(buf_size) &&
				HT_IO_ALIGNED(start_offset) &&
				HT_IO_ALIGNED(end_offset)));
		int fd = open(name, flags | OPEN_FLAG_VERIFY_CHECKSUM);
		{
			lock_guard<mutex> lock(m_mutex);
			HT_ASSERT(m_buffered_reader_map.find(fd) == m_buffered_reader_map.end());
			m_buffered_reader_map[fd] =
				new ClientBufferedReaderHandler(this, fd, buf_size, outstanding,
					start_offset, end_offset);
		}
		return fd;
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error opening buffered FS file=%s buf_size=%u "
			"outstanding=%u start_offset=%llu end_offset=%llu", name.c_str(),
			buf_size, outstanding, (Llu)start_offset, (Llu)end_offset);
	}
}

void Client::decode_response_open(EventPtr &event, int32_t *fd) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Open params;
	params.decode(&ptr, &remain);
	*fd = params.get_fd();
}


void
Client::create(const String &name, uint32_t flags, int32_t bufsz,
	int32_t replication, int64_t blksz,
	DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_CREATE);
	Request::Parameters::Create params(name, flags, bufsz, replication, blksz);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, handler);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error creating FS file: %s:", name.c_str());
	}
}


int
Client::create(const String &name, uint32_t flags, int32_t bufsz,
	int32_t replication, int64_t blksz) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_CREATE);
	Request::Parameters::Create params(name, flags, bufsz, replication, blksz);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		int32_t fd;
		decode_response_create(event, &fd);
		return fd;
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error creating FS file: %s", name.c_str());
	}
}


void Client::decode_response_create(EventPtr &event, int32_t *fd) {
	decode_response_open(event, fd);
}


void
Client::close(int32_t fd, DispatchHandler *handler) {
	ClientBufferedReaderHandler *reader_handler = 0;
	CommHeader header(Request::Handler::Factory::FUNCTION_CLOSE);
	header.gid = fd;
	Request::Parameters::Close params(fd);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	{
		lock_guard<mutex> lock(m_mutex);
		auto iter = m_buffered_reader_map.find(fd);
		if (iter != m_buffered_reader_map.end()) {
			reader_handler = (*iter).second;
			m_buffered_reader_map.erase(iter);
		}
	}
	delete reader_handler;

	try {
		send_message(cbuf, handler);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error closing FS fd: %d", (int)fd);
	}
}


void
Client::close(int32_t fd) {
	ClientBufferedReaderHandler *reader_handler = 0;
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_CLOSE);
	header.gid = fd;
	Request::Parameters::Close params(fd);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());
	{
		lock_guard<mutex> lock(m_mutex);
		auto iter = m_buffered_reader_map.find(fd);
		if (iter != m_buffered_reader_map.end()) {
			reader_handler = (*iter).second;
			m_buffered_reader_map.erase(iter);
		}
	}
	delete reader_handler;

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error closing FS fd: %d", (int)fd);
	}
}


void
Client::read(int32_t fd, size_t len, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_READ);
	header.gid = fd;
	Request::Parameters::Read params(fd, len);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, handler);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending read request for %u bytes "
			"from FS fd: %d", (unsigned)len, (int)fd);
	}
}


size_t
Client::read(int32_t fd, void *dst, size_t len) {
	ClientBufferedReaderHandler *reader_handler = 0;
	{
		lock_guard<mutex> lock(m_mutex);
		auto iter = m_buffered_reader_map.find(fd);
		if (iter != m_buffered_reader_map.end())
			reader_handler = (*iter).second;
	}
	try {
		if (reader_handler)
			return reader_handler->read(dst, len);

		DispatchHandlerSynchronizer sync_handler;
		EventPtr event;
		CommHeader header(Request::Handler::Factory::FUNCTION_READ);
		header.gid = fd;
		Request::Parameters::Read params(fd, len);
		CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
		params.encode(cbuf->get_data_ptr_address());
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		uint32_t length;
		uint64_t offset;
		const void *data;
		decode_response_read(event, &data, &offset, &length);
		HT_ASSERT(length <= len);
		memcpy(dst, data, length);
		return length;
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error reading %u bytes from FS fd %d",
			(unsigned)len, (int)fd);
	}
}

void Client::decode_response_read(EventPtr &event, const void **buffer,
	uint64_t *offset, uint32_t *length) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Read params;
	params.decode(&ptr, &remain);
	*offset = params.get_offset();
	*length = params.get_amount();

	if (*length == (uint32_t)-1) {
		*length = 0;
		return;
	}

	if (remain < (size_t)*length)
		HT_THROWF(Error::RESPONSE_TRUNCATED, "%lu < %lu", (Lu)remain, (Lu)*length);

	*buffer = ptr;
}

void
Client::append(int32_t fd, StaticBuffer &buffer, Flags flags,
	DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_APPEND);
	header.gid = fd;
	header.alignment = HT_DIRECT_IO_ALIGNMENT;
	CommBuf *cbuf = new CommBuf(header, HT_DIRECT_IO_ALIGNMENT, buffer);
	Request::Parameters::Append params(fd, buffer.size, static_cast<uint8_t>(flags));
	uint8_t *base = (uint8_t *)cbuf->get_data_ptr();
	params.encode(cbuf->get_data_ptr_address());
	size_t padding = HT_DIRECT_IO_ALIGNMENT -
		(((uint8_t *)cbuf->get_data_ptr()) - base);
	memset(cbuf->get_data_ptr(), 0, padding);
	cbuf->advance_data_ptr(padding);

	CommBufPtr cbp(cbuf);

	try {
		send_message(cbp, handler);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error appending %u bytes to FS fd %d",
			(unsigned)buffer.size, (int)fd);
	}
}


size_t Client::append(int32_t fd, StaticBuffer &buffer, Flags flags) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;

	CommHeader header(Request::Handler::Factory::FUNCTION_APPEND);
	header.gid = fd;
	header.alignment = HT_DIRECT_IO_ALIGNMENT;
	CommBuf *cbuf = new CommBuf(header, HT_DIRECT_IO_ALIGNMENT, buffer);
	Request::Parameters::Append params(fd, buffer.size, static_cast<uint8_t>(flags));
	uint8_t *base = (uint8_t *)cbuf->get_data_ptr();
	params.encode(cbuf->get_data_ptr_address());
	size_t padding = HT_DIRECT_IO_ALIGNMENT -
		(((uint8_t *)cbuf->get_data_ptr()) - base);
	memset(cbuf->get_data_ptr(), 0, padding);
	cbuf->advance_data_ptr(padding);

	CommBufPtr cbp(cbuf);

	try {
		send_message(cbp, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		uint64_t offset;
		uint32_t amount;
		decode_response_append(event, &offset, &amount);

		if (buffer.size != amount)
			HT_THROWF(Error::FSBROKER_IO_ERROR, "tried to append %u bytes but got "
				"%u", (unsigned)buffer.size, (unsigned)amount);
		return (size_t)amount;
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error appending %u bytes to FS fd %d",
			(unsigned)buffer.size, (int)fd);
	}
}


void Client::decode_response_append(EventPtr &event, uint64_t *offset,
	uint32_t *length) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Append params;
	params.decode(&ptr, &remain);
	*offset = params.get_offset();
	*length = params.get_amount();
}


void
Client::seek(int32_t fd, uint64_t offset, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_SEEK);
	header.gid = fd;
	Request::Parameters::Seek params(fd, offset);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error seeking to %llu on FS fd %d",
			(Llu)offset, (int)fd);
	}
}


void
Client::seek(int32_t fd, uint64_t offset) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_SEEK);
	header.gid = fd;
	Request::Parameters::Seek params(fd, offset);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error seeking to %llu on FS fd %d",
			(Llu)offset, (int)fd);
	}
}


void
Client::remove(const String &name, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_REMOVE);
	Request::Parameters::Remove params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error removing FS file: %s", name.c_str());
	}
}


void
Client::remove(const String &name, bool force) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_REMOVE);
	Request::Parameters::Remove params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event)) {
			int error = Protocol::response_code(event.get());
			if (!force || error != Error::FSBROKER_FILE_NOT_FOUND)
				HT_THROW(error, Protocol::string_format_message(event).c_str());
		}
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error removing FS file: %s", name.c_str());
	}
}


void
Client::shutdown(uint16_t flags, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_SHUTDOWN);
	Request::Parameters::Shutdown params(flags);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "sending FS shutdown (flags=%d)", (int)flags);
	}
}


void Client::status(Status &status, Timer *timer) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_STATUS);
	CommBufPtr cbuf(new CommBuf(header));

	try {
		send_message(cbuf, &sync_handler, timer);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		decode_response_status(event, status);
	}
	catch (Exception &e) {
		status.set(Status::Code::CRITICAL,
			format("%s - %s", Error::get_text(e.code()), e.what()));
	}
}

void Client::decode_response_status(EventPtr &event, Status &status) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Status params;
	params.decode(&ptr, &remain);
	status = params.status();
}



void Client::length(const String &name, bool accurate,
	DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_LENGTH);
	Request::Parameters::Length params(name, accurate);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending length request for FS file: %s",
			name.c_str());
	}
}


int64_t Client::length(const String &name, bool accurate) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_LENGTH);
	Request::Parameters::Length params(name, accurate);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		return decode_response_length(event);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error getting length of FS file: %s",
			name.c_str());
	}
}

int64_t Client::decode_response_length(EventPtr &event) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Length params;
	params.decode(&ptr, &remain);
	return params.get_length();
}


void
Client::pread(int32_t fd, size_t len, uint64_t offset,
	bool verify_checksum, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_PREAD);
	header.gid = fd;
	Request::Parameters::Pread params(fd, offset, len, verify_checksum);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending pread request at byte %llu "
			"on FS fd %d", (Llu)offset, (int)fd);
	}
}


size_t
Client::pread(int32_t fd, void *dst, size_t len, uint64_t offset, bool verify_checksum) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_PREAD);
	header.gid = fd;
	Request::Parameters::Pread params(fd, offset, len, verify_checksum);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		uint32_t length;
		uint64_t off;
		const void *data;
		decode_response_pread(event, &data, &off, &length);
		HT_ASSERT(length <= len);
		memcpy(dst, data, length);
		return length;
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error preading at byte %llu on FS fd %d",
			(Llu)offset, (int)fd);
	}
}

void Client::decode_response_pread(EventPtr &event, const void **buffer,
	uint64_t *offset, uint32_t *length) {
	decode_response_read(event, buffer, offset, length);
}

void Client::mkdirs(const String &name, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_MKDIRS);
	Request::Parameters::Mkdirs params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending mkdirs request for FS "
			"directory: %s", name.c_str());
	}
}


void
Client::mkdirs(const String &name) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_MKDIRS);
	Request::Parameters::Mkdirs params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error mkdirs FS directory %s", name.c_str());
	}
}


void
Client::flush(int32_t fd, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_FLUSH);
	header.gid = fd;
	Request::Parameters::Flush params(fd);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error flushing FS fd %d", (int)fd);
	}
}


void
Client::flush(int32_t fd) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_FLUSH);
	header.gid = fd;
	Request::Parameters::Flush params(fd);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error flushing FS fd %d", (int)fd);
	}
}


void
Client::sync(int32_t fd) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_SYNC);
	header.gid = fd;
	Request::Parameters::Sync params(fd);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error syncing FS fd %d", (int)fd);
	}
}


void Client::rmdir(const String &name, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_RMDIR);
	Request::Parameters::Rmdir params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending rmdir request for FS directory: "
			"%s", name.c_str());
	}
}


void
Client::rmdir(const String &name, bool force) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_RMDIR);
	Request::Parameters::Rmdir params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event)) {
			int error = Protocol::response_code(event.get());

			if (!force || error != Error::FSBROKER_FILE_NOT_FOUND)
				HT_THROW(error, Protocol::string_format_message(event).c_str());
		}
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error removing FS directory: %s", name.c_str());
	}
}


void Client::readdir(const String &name, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_READDIR);
	Request::Parameters::Readdir params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending readdir request for FS directory"
			": %s", name.c_str());
	}
}


void Client::readdir(const String &name, std::vector<Dirent> &listing) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_READDIR);
	Request::Parameters::Readdir params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		decode_response_readdir(event, listing);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error reading directory entries for FS "
			"directory: %s", name.c_str());
	}
}


void Client::decode_response_readdir(EventPtr &event,
	std::vector<Dirent> &listing) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Readdir params;
	params.decode(&ptr, &remain);
	params.get_listing(listing);
}


void Client::exists(const String &name, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_EXISTS);
	Request::Parameters::Exists params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "sending 'exists' request for FS path: %s",
			name.c_str());
	}
}


bool Client::exists(const String &name) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_EXISTS);
	Request::Parameters::Exists params(name);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());

		return decode_response_exists(event);
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error checking existence of FS path: %s",
			name.c_str());
	}
}


bool Client::decode_response_exists(EventPtr &event) {
	int error = Protocol::response_code(event);
	if (error != Error::OK)
		HT_THROW(error, Protocol::string_format_message(event));

	const uint8_t *ptr = event->payload + 4;
	size_t remain = event->payload_len - 4;

	Response::Parameters::Exists params;
	params.decode(&ptr, &remain);
	return params.get_exists();
}



void
Client::rename(const String &src, const String &dst, DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_RENAME);
	Request::Parameters::Rename params(src, dst);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending 'rename' request for FS "
			"path: %s -> %s", src.c_str(), dst.c_str());
	}
}


void
Client::rename(const String &src, const String &dst) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_RENAME);
	Request::Parameters::Rename params(src, dst);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length()));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error renaming of FS path: %s -> %s",
			src.c_str(), dst.c_str());
	}
}


void
Client::debug(int32_t command, StaticBuffer &serialized_parameters,
	DispatchHandler *handler) {
	CommHeader header(Request::Handler::Factory::FUNCTION_DEBUG);
	Request::Parameters::Debug params(command);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length(),
		serialized_parameters));
	params.encode(cbuf->get_data_ptr_address());

	try { send_message(cbuf, handler); }
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending debug command %d request", command);
	}
}


void
Client::debug(int32_t command, StaticBuffer &serialized_parameters) {
	DispatchHandlerSynchronizer sync_handler;
	EventPtr event;
	CommHeader header(Request::Handler::Factory::FUNCTION_DEBUG);
	Request::Parameters::Debug params(command);
	CommBufPtr cbuf(new CommBuf(header, params.encoded_length(),
		serialized_parameters));
	params.encode(cbuf->get_data_ptr_address());

	try {
		send_message(cbuf, &sync_handler);

		if (!sync_handler.wait_for_reply(event))
			HT_THROW(Protocol::response_code(event.get()),
				Protocol::string_format_message(event).c_str());
	}
	catch (Exception &e) {
		HT_THROW2F(e.code(), e, "Error sending debug command %d request", command);
	}
}



void
Client::send_message(CommBufPtr &cbuf, DispatchHandler *handler, Timer *timer) {
	uint32_t deadline = timer ? timer->remaining() : m_timeout_ms;
	int error = m_comm->send_request(m_addr, deadline, cbuf, handler);

	if (error != Error::OK)
		HT_THROWF(error, "FS send_request to %s failed", m_addr.format().c_str());
}