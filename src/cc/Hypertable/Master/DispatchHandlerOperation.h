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

#ifndef HYPERTABLE_DISPATCHHANDLEROPERATION_H
#define HYPERTABLE_DISPATCHHANDLEROPERATION_H

#include "AsyncComm/Comm.h"
#include "AsyncComm/DispatchHandler.h"

#include "Common/StringExt.h"

#include "Hypertable/Lib/RangeServerClient.h"
#include "Hypertable/Lib/Types.h"

#include "Context.h"

namespace Hypertable {

  /**
   * This class is a DispatchHandler class that is used for managing
   * asynchronous operations
   */
  class DispatchHandlerOperation : public DispatchHandler {

  public:

    struct Result {
      String location;
      int error;
      String msg;
    };

    DispatchHandlerOperation(ContextPtr &context);

    void start(StringSet &locations);

    virtual void start(const String &location) = 0;

    virtual void result_callback(EventPtr &event) { }

    virtual void handle(EventPtr &event);

    bool wait_for_completion();

    void get_results(std::vector<Result> &results);

    void process_events();

  protected:
    ContextPtr m_context;
    RangeServerClient m_rsclient;

  private:
    Mutex m_mutex;
    boost::condition m_cond;
    std::vector<EventPtr> m_events;
    int m_outstanding;
    int m_error_count;
    StringSet m_locations;
    std::vector<Result> m_results;
  };

  typedef intrusive_ptr<DispatchHandlerOperation> DispatchHandlerOperationPtr;
  
}


#endif // HYPERTABLE_DISPATCHHANDLEROPERATION_H
