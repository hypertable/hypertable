/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
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

#include "Common/Compat.h"

#include <cmath>

#include "Common/Error.h"
#include "Common/FailureInducer.h"
#include "Common/Serialization.h"
#include "Common/StringExt.h"
#include "Common/Time.h"

#include <boost/algorithm/string.hpp>

#include "OperationRegisterServer.h"
#include "OperationProcessor.h"

using namespace Hypertable;


OperationRegisterServer::OperationRegisterServer(ContextPtr &context, EventPtr &event)
  : Operation(context, event, MetaLog::EntityType::OPERATION_REGISTER_SERVER) {

  const uint8_t *ptr = event->payload;
  size_t remaining = event->payload_len;
  decode_request(&ptr, &remaining);

  m_local_addr = InetAddr(event->addr);
  m_public_addr = InetAddr(m_system_stats.net_info.primary_addr, m_listen_port);
  m_received_ts = get_ts64();

}


void OperationRegisterServer::execute() {

  if (m_location == "") {
    if (!m_context->find_server_by_hostname(m_system_stats.net_info.host_name, m_rsc))
      m_context->find_server_by_public_addr(m_public_addr, m_rsc);
    if (m_rsc)
      m_location = m_rsc->location();
  }
  else
    m_context->find_server_by_location(m_location, m_rsc);

  if (!m_rsc) {
    if (m_location == "") {
      uint64_t id = m_context->hyperspace->attr_incr(m_context->master_file_handle, "next_server_id");
      if (m_context->location_hash.empty())
        m_location = String("rs") + id;
      else
        m_location = format("rs-%s-%llu", m_context->location_hash.c_str(), (Llu)id);
    }
    bool balanced = false;
    if (!m_context->in_operation)
      balanced = true;
    m_rsc = new RangeServerConnection(m_context->mml_writer, m_location,
                                      m_system_stats.net_info.host_name, m_public_addr,
                                      balanced);
  }

  m_context->connect_server(m_rsc, m_system_stats.net_info.host_name,
                            m_local_addr, m_public_addr);
  int32_t difference = (int32_t)abs((m_received_ts - m_register_ts)/ 1000LL);
  if (difference > (3000000+m_context->max_allowable_skew)) {
    m_error = Error::RANGESERVER_CLOCK_SKEW;
    m_error_msg = format("Detected clock skew while registering server %s(%s), as location %s register_ts=%llu, received_ts=%llu, difference=%d > allowable skew %d",
        m_system_stats.net_info.host_name.c_str(), m_public_addr.format().c_str(),
        m_location.c_str(), (Llu)m_register_ts, (Llu)m_received_ts, difference,
        m_context->max_allowable_skew);
    HT_ERROR_OUT << m_error_msg << HT_END;
    // clock skew detected by master
    CommHeader header;
    header.initialize_from_request_header(m_event->header);
    header.command = RangeServerProtocol::COMMAND_INITIALIZE;
    CommBufPtr cbp(new CommBuf(header, encoded_result_length()));

    encode_result(cbp->get_data_ptr_address());
    int error = m_context->comm->send_response(m_event->addr, cbp);
    if (error != Error::OK)
      HT_ERRORF("Problem sending response (location=%s) back to %s",
                m_location.c_str(), m_event->addr.format().c_str());
  }
  else {
    m_context->monitoring->add_server(m_location, m_system_stats);
    HT_INFOF("%lld Registering server %s (host=%s, local_addr=%s, public_addr=%s)",
        (Lld)header.id, m_rsc->location().c_str(), m_system_stats.net_info.host_name.c_str(),
        m_local_addr.format().c_str(), m_public_addr.format().c_str());

    /** Send back Response **/
    {
      CommHeader header;
      header.initialize_from_request_header(m_event->header);
      header.command = RangeServerProtocol::COMMAND_INITIALIZE;
      CommBufPtr cbp(new CommBuf(header, encoded_result_length()));
      encode_result(cbp->get_data_ptr_address());
      int error = m_context->comm->send_response(m_event->addr, cbp);
      if (error != Error::OK)
        HT_ERRORF("Problem sending response (location=%s) back to %s",
            m_location.c_str(), m_event->addr.format().c_str());
    }
  }
  complete_ok_no_log();
  m_context->op->unblock(m_location);
  m_context->op->unblock(Dependency::SERVERS);
  HT_INFOF("%lld Leaving RegisterServer %s", (Lld)header.id, m_rsc->location().c_str());
}

size_t OperationRegisterServer::encoded_result_length() const {
  size_t length = Operation::encoded_result_length();
  if (m_error == Error::OK)
    length += Serialization::encoded_length_vstr(m_location);
  return length;
}

void OperationRegisterServer::encode_result(uint8_t **bufp) const {
  Operation::encode_result(bufp);
  if (m_error == Error::OK)
    Serialization::encode_vstr(bufp, m_location);
}

void OperationRegisterServer::decode_result(const uint8_t **bufp, size_t *remainp) {
  Operation::decode_result(bufp, remainp);
  if (m_error == Error::OK)
    m_location = Serialization::decode_vstr(bufp, remainp);
}

void OperationRegisterServer::display_state(std::ostream &os) {
  os << " location=" << m_location << " host=" << m_system_stats.net_info.host_name;
  os << " register_ts=" << m_register_ts;
  os << " local_addr=" << m_rsc->local_addr().format();
  os << " public_addr=" << m_rsc->public_addr().format() << " ";
}

void OperationRegisterServer::decode_request(const uint8_t **bufp, size_t *remainp) {
  m_location = Serialization::decode_vstr(bufp, remainp);
  m_listen_port = Serialization::decode_i16(bufp, remainp);
  m_system_stats.decode(bufp, remainp);
  m_register_ts = Serialization::decode_i64(bufp, remainp);
}

const String OperationRegisterServer::name() {
  return "OperationRegisterServer";
}

const String OperationRegisterServer::label() {
  return String("RegisterServer ") + m_location;
}

