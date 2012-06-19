/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Common/Compat.h"
#include "Common/StringExt.h"

#include <sstream>

#include "boost/graph/topological_sort.hpp"
#include "boost/graph/graphviz.hpp"

#include "OperationInitialize.h"
#include "OperationProcessor.h"

using namespace Hypertable;
using namespace boost;


OperationProcessor::ThreadContext::ThreadContext(ContextPtr &mctx)
  : master_context(mctx), current_blocked(0), busy_count(0),
    need_order_recompute(false), shutdown(false), paused(false) {
  current_iter = current.end();
  execution_order_iter = execution_order.end();
}

OperationProcessor::ThreadContext::~ThreadContext() {
}


OperationProcessor::OperationProcessor(ContextPtr &context, size_t thread_count) 
  : m_context(context) {
  m_context.execution_order_iter = m_context.execution_order.end();
  m_context.op = this;
  Worker worker(m_context);
  for (size_t i=0; i<thread_count; ++i)
    m_threads.create_thread(worker);
}


void OperationProcessor::add_operation(OperationPtr &operation) {
  ScopedLock lock(m_context.mutex);

  //HT_INFOF("Adding operation %s", operation->label().c_str());

  if (!operation->is_complete() || operation->is_perpetual()) {
    add_operation_internal(operation);
    m_context.need_order_recompute = true;
    m_context.current_iter = m_context.current.end();
    m_context.cond.notify_all();
  }
  else if (!operation->remove_explicitly())
    m_context.master_context->response_manager->add_operation(operation);

}

void OperationProcessor::add_operations(std::vector<OperationPtr> &operations) {
  ScopedLock lock(m_context.mutex);
  bool added = false;

  for (size_t i=0; i<operations.size(); i++) {

    //HT_INFOF("Adding operation %s", operations[i]->label().c_str());

    if (!operations[i]->is_complete() || operations[i]->is_perpetual()) {
      add_operation_internal(operations[i]);
      added = true;
    }
    else if (!operations[i]->remove_explicitly())
      m_context.master_context->response_manager->add_operation(operations[i]);
  }

  if (added) {
    m_context.need_order_recompute = true;
    m_context.current_iter = m_context.current.end();
    m_context.cond.notify_all();
  }
}

void OperationProcessor::add_operation_internal(OperationPtr &operation) {

  if (operation->exclusive()) {
    if (m_context.exclusive_ops.count(operation->name()) > 0)
      HT_THROWF(Error::MASTER_OPERATION_IN_PROGRESS,
                "Dropping %s because another one is outstanding",
                operation->name().c_str());
    m_context.exclusive_ops.insert(operation->name());
  }

  Vertex v = add_vertex(m_context.graph);
  put(m_context.label, v, operation->graphviz_label());
  put(m_context.exec_time, v, 0);
  put(m_context.ops, v, operation);
  put(m_context.busy, v, false);
  m_context.live.insert(v);

  add_dependencies(v, operation);
}

void OperationProcessor::shutdown() {
  ScopedLock lock(m_context.mutex);
  m_context.shutdown = true;
  m_context.cond.notify_all();
}

void OperationProcessor::join() {
  m_threads.join_all();
}

void OperationProcessor::wait_for_empty() {
  ScopedLock lock(m_context.mutex);
  while (num_vertices(m_context.graph) > 0)
    m_context.idle_cond.wait(lock);
}

void OperationProcessor::wait_for_idle() {
  ScopedLock lock(m_context.mutex);
  while (m_context.busy_count > 0 ||
         m_context.need_order_recompute ||
         m_context.execution_order_iter != m_context.execution_order.end())
    m_context.idle_cond.wait(lock);
}

bool OperationProcessor::timed_wait_for_idle(boost::xtime expire_time) {
  ScopedLock lock(m_context.mutex);
  while (m_context.busy_count > 0 ||
         m_context.need_order_recompute ||
         m_context.execution_order_iter != m_context.execution_order.end()) {
    if (!m_context.idle_cond.timed_wait(lock, expire_time))
      return false;
  }
  return true;
}

size_t OperationProcessor::size() {
  return num_vertices(m_context.graph);
}

bool OperationProcessor::empty() {
  return num_vertices(m_context.graph) == 0;
}

void OperationProcessor::wake_up() {
  ScopedLock lock(m_context.mutex);
  m_context.need_order_recompute = true;
  m_context.cond.notify_all();
}

void OperationProcessor::unblock(const String &name) {
  ScopedLock lock(m_context.mutex);
  std::pair<DependencyIndex::iterator, DependencyIndex::iterator> bound;
  bool unblocked_something = false;

  for (bound = m_context.obstruction_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    if (m_context.ops[bound.first->second]->unblock())
      unblocked_something = true;

  for (bound = m_context.exclusivity_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    if (m_context.ops[bound.first->second]->unblock())
      unblocked_something = true;

  for (bound = m_context.dependency_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    if (m_context.ops[bound.first->second]->unblock())
      unblocked_something = true;

  if (unblocked_something) {
    m_context.current_blocked = 0;
    m_context.need_order_recompute = true;
    m_context.cond.notify_all();
  }

}

/**
 *
 */
void OperationProcessor::Worker::operator()() {
  Vertex vertex;
  OperationPtr operation;
  bool current_needs_loading = true;
  
  try {

    while (true) {
      {
        ScopedLock lock(m_context.mutex);

        while (m_context.current_iter == m_context.current.end()) {

          if (m_context.need_order_recompute) {
            recompute_order();
            current_needs_loading = true;
          }
          else
            current_needs_loading = 
              m_context.current_active.empty() && m_context.current_blocked == 0;

          if (current_needs_loading &&
              m_context.execution_order_iter != m_context.execution_order.end()) {
            if (load_current()) {
              if (m_context.shutdown)
                return;
              continue;
            }
          }

          if (m_context.busy_count == 0)
            m_context.idle_cond.notify_all();

          if (m_context.shutdown)
            return;

          m_context.cond.wait(lock);

          if (m_context.shutdown)
            return;
        }

        vertex = m_context.current_iter->vertex;
        ++m_context.current_iter;
        operation = m_context.ops[vertex];
        m_context.busy[vertex] = true;
        m_context.busy_count++;
      }

      try {

        if (!operation->is_blocked())
          operation->execute();

        {
          ScopedLock lock(m_context.mutex);
          m_context.busy[vertex] = false;
          m_context.busy_count--;
          m_context.current_active.erase(vertex);
          if (operation->is_complete())
            retire_operation(vertex, operation);
          else if (operation->is_blocked())
            m_context.current_blocked++;
          else
            update_operation(vertex, operation);
        }
      }
      catch (Exception &e) {
        ScopedLock lock(m_context.mutex);
        m_context.busy[vertex] = false;
        m_context.busy_count--;
        m_context.current_active.erase(vertex);
        if (e.code() == Error::INDUCED_FAILURE) {
          m_context.shutdown = true;
          m_context.cond.notify_all();
          m_context.master_context->mml_writer->close();
          break;
        }
        HT_ERROR_OUT << e << HT_END;
        poll(0, 0, 5000);
        m_context.need_order_recompute = true;
      }

    }
  }
  catch (std::exception &e) {
    std::cout << e.what() << std::endl;
  }
}


void OperationProcessor::add_dependencies(Vertex v, OperationPtr &operation) {
  DependencySet names;

  operation->exclusivities(names);
  for (DependencySet::iterator iter = names.begin(); iter != names.end(); ++iter)
    add_exclusivity(v, *iter);

  operation->dependencies(names);
  for (DependencySet::iterator iter = names.begin(); iter != names.end(); ++iter)
    add_dependency(v, *iter);

  operation->obstructions(names);
  for (DependencySet::iterator iter = names.begin(); iter != names.end(); ++iter) {
    add_obstruction(v, *iter);
  }

  // Add sub-operations
  std::vector<Operation *> sub_ops;
  operation->swap_sub_operations(sub_ops);
  OperationPtr sub_op;
  for (size_t i=0; i<sub_ops.size(); i++) {
    sub_op = sub_ops[i];
    m_context.op->add_operation_internal(sub_op);
  }

}



void OperationProcessor::add_exclusivity(Vertex v, const String &name) {
  GraphTraits::in_edge_iterator in_i, in_end;
  DependencySet names;
  Vertex src;
  std::pair<DependencyIndex::iterator, DependencyIndex::iterator> bound;

  // Check obstructions index and add link (v -> obstruction)
  for (bound = m_context.obstruction_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    add_edge(v, bound.first->second);

  // Check dependency index and add link (dependency -> v)
  for (bound = m_context.dependency_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    add_edge(bound.first->second, v);

  // Return now if this exclusivity already exists
  for (bound = m_context.exclusivity_index.equal_range(name);
       bound.first != bound.second; ++bound.first) {
    if (v == bound.first->second)
      return;
  }

  for (DependencyIndex::iterator iter = m_context.exclusivity_index.lower_bound(name);
       iter != m_context.exclusivity_index.end() && iter->first == name; ++iter) {
    tie(in_i, in_end) = in_edges(iter->second, m_context.graph);
    for (; in_i != in_end; ++in_i) {
      src = source(*in_i, m_context.graph);
      m_context.ops[src]->exclusivities(names);
      if (names.find(name) != names.end())
        break;
    }
    if (in_i == in_end) {
      HT_ASSERT(v != iter->second);
      add_edge_permanent(v, iter->second);
      break;
    }
  }

  m_context.exclusivity_index.insert(DependencyIndex::value_type(name, v));
}


void OperationProcessor::add_dependency(Vertex v, const String &name) {
  std::pair<DependencyIndex::iterator, DependencyIndex::iterator> bound;

  // Return immediately if dependency already exists
  for (bound = m_context.dependency_index.equal_range(name);
       bound.first != bound.second; ++bound.first) {
    if (bound.first->second == v)
      return;
  }

  // Check exclusivity index and add link (v -> exclusivity)
  for (bound = m_context.exclusivity_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    add_edge(v, bound.first->second);

  // Add perpetual operations if necessary
  if (!m_context.perpetual_ops.empty()) {
    DependencySet names;
    PerpetualSet::iterator iter = m_context.perpetual_ops.begin();
    OperationPtr operation;
    while (iter != m_context.perpetual_ops.end()) {
      (*iter)->obstructions(names);
      if (names.count(name) > 0) {
        operation = *iter++;
        operation->set_state(OperationState::INITIAL);
        add_operation_internal(operation);
      }
      else
        ++iter;
    }
  }

  // Check obstruction index and add link (v -> obstruction)
  for (bound = m_context.obstruction_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    add_edge(v, bound.first->second);

  m_context.dependency_index.insert(DependencyIndex::value_type(name, v));
}


void OperationProcessor::add_obstruction(Vertex v, const String &name) {
  std::pair<DependencyIndex::iterator, DependencyIndex::iterator> bound;

  // Return immediately if obstruction already exists
  for (bound = m_context.obstruction_index.equal_range(name);
       bound.first != bound.second; ++bound.first) {
    if (bound.first->second == v)
      return;
  }

  // Check exclusivity index and add link (exclusivity -> v)
  for (bound = m_context.exclusivity_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    add_edge(bound.first->second, v);

  // Check dependency index and add link (dependency -> v)
  for (bound = m_context.dependency_index.equal_range(name);
       bound.first != bound.second; ++bound.first)
    add_edge(bound.first->second, v);

  m_context.obstruction_index.insert(DependencyIndex::value_type(name, v));
}


void OperationProcessor::purge_from_dependency_index(Vertex v) {
  DependencyIndex::iterator iter, del_iter;

  // Purge from dependency index
  iter = m_context.dependency_index.begin();
  while (iter != m_context.dependency_index.end()) {
    if (iter->second == v) {
      del_iter = iter++;
      m_context.dependency_index.erase(del_iter);
    }
    else
      ++iter;
  }
}


void OperationProcessor::purge_from_exclusivity_index(Vertex v) {
  DependencyIndex::iterator iter, del_iter;

  // Purge from exclusivity index
  iter = m_context.exclusivity_index.begin();
  while (iter != m_context.exclusivity_index.end()) {
    if (iter->second == v) {
      del_iter = iter++;
      m_context.exclusivity_index.erase(del_iter);
    }
    else
      ++iter;
  }
}


void OperationProcessor::purge_from_obstruction_index(Vertex v) {
  DependencyIndex::iterator iter, del_iter;

  // Purge from obstruction index
  iter = m_context.obstruction_index.begin();
  while (iter != m_context.obstruction_index.end()) {
    if (iter->second == v) {
      del_iter = iter++;
      m_context.obstruction_index.erase(del_iter);
    }
    else
      ++iter;
  }
}


void OperationProcessor::add_edge(Vertex v, Vertex u) {
  std::pair<Edge, bool> ep = ::add_edge(v, u, m_context.graph);
  HT_ASSERT(ep.second);
  put(m_context.permanent, ep.first, false);
}

void OperationProcessor::add_edge_permanent(Vertex v, Vertex u) {
  std::pair<Edge, bool> ep = ::add_edge(v, u, m_context.graph);
  HT_ASSERT(ep.second);
  put(m_context.permanent, ep.first, true);
}

void OperationProcessor::graphviz_output(String &output) {
  ScopedLock lock(m_context.mutex);
  std::ostringstream oss;
  write_graphviz(oss, m_context.graph, make_label_writer(m_context.label));
  output = oss.str();
}


void OperationProcessor::Worker::retire_operation(Vertex v, OperationPtr &operation) {
  m_context.op->purge_from_obstruction_index(v);
  m_context.op->purge_from_dependency_index(v);
  m_context.op->purge_from_exclusivity_index(v);
  if (in_degree(v, m_context.graph) > 0)
    m_context.need_order_recompute = true;
  clear_vertex(v, m_context.graph);
  remove_vertex(v, m_context.graph);
  m_context.live.erase(v);
  if (operation->exclusive())
    m_context.exclusive_ops.erase(operation->name());
  //HT_INFOF("Retiring op %p vertex %p", operation.get(), v);

  if (operation->is_perpetual())
    m_context.perpetual_ops.insert(operation);
  else {
    if (!operation->remove_explicitly())
      m_context.master_context->response_manager->add_operation(operation);
  }

}


void OperationProcessor::Worker::update_operation(Vertex v, OperationPtr &operation) {
  not_permanent np(m_context);

  m_context.op->purge_from_obstruction_index(v);
  m_context.op->purge_from_dependency_index(v);

  remove_in_edge_if(v, np, m_context.graph);
  remove_out_edge_if(v, np, m_context.graph);

  m_context.op->add_dependencies(v, operation);
  
  m_context.need_order_recompute = true;
  m_context.current_iter = m_context.current.end();
  m_context.cond.notify_all();
}


void OperationProcessor::Worker::recompute_order() {

  // re-assign vertex indexes
  property_map<OperationGraph, vertex_index_t>::type index = get(vertex_index, m_context.graph);
  int i=0;
  std::pair<GraphTraits::vertex_iterator, GraphTraits::vertex_iterator> vp;
  for (vp = vertices(m_context.graph); vp.first != vp.second; ++vp.first)
    put(index, *vp.first, i++);

  /** uncomment for DEBUG output **/
  //write_graphviz(std::cout, m_context.graph, make_label_writer(m_context.label));

  m_context.execution_order.clear();
  topological_sort(m_context.graph, std::back_inserter(m_context.execution_order));

  ExecutionList::iterator iter;

  for (iter = m_context.execution_order.begin(); iter != m_context.execution_order.end(); ++iter) {
    HT_ASSERT(m_context.live.count(iter->vertex) > 0);
    //HT_ASSERT(!m_context.ops[*iter]->is_complete());
    m_context.exec_time[iter->vertex] = 0;
  }

  for (iter = m_context.execution_order.begin(); iter != m_context.execution_order.end(); ++iter) {
    // Walk through the out_edges an calculate the maximum time.
    if (out_degree (iter->vertex, m_context.graph) > 0) {
      OperationGraph::out_edge_iterator j, j_end;
      int maxdist=0;
      for (boost::tie(j, j_end) = out_edges(iter->vertex, m_context.graph); j != j_end; ++j)
        maxdist=(std::max)(m_context.exec_time[target(*j, m_context.graph)], maxdist);
      m_context.exec_time[iter->vertex] = maxdist+1;
    }
  }

  // Sort execution order
  std::vector<struct vertex_info> vvec;
  for (iter = m_context.execution_order.begin(); iter != m_context.execution_order.end(); ++iter) {
    iter->taken = m_context.busy[iter->vertex];
    vvec.push_back(*iter);
  }

  std::set<Vertex> vset;

  struct ltvertexinfo ltvi(m_context);
  std::sort(vvec.begin(), vvec.end(), ltvi);
  m_context.execution_order.clear();
  for (size_t i=0; i<vvec.size(); i++) {
    HT_ASSERT(vset.count(vvec[i].vertex) == 0);
    vset.insert(vvec[i].vertex);
    m_context.execution_order.push_back(vvec[i]);
  }

  m_context.execution_order_iter = m_context.execution_order.begin();

  m_context.need_order_recompute = false;
  if (m_context.execution_order.empty())
    m_context.idle_cond.notify_all();

}


bool OperationProcessor::Worker::load_current() {
  size_t blocked = 0;
  size_t retired = 0;

  m_context.current.clear();
  m_context.current_active.clear();
  m_context.current_blocked = 0;
  for (int time_slot = m_context.exec_time[m_context.execution_order_iter->vertex];
       m_context.execution_order_iter != m_context.execution_order.end() && time_slot == m_context.exec_time[m_context.execution_order_iter->vertex];
       ++m_context.execution_order_iter) {
    // If live, add it to the current set
    if (m_context.live.count(m_context.execution_order_iter->vertex) > 0) {
      m_context.current.push_back(*m_context.execution_order_iter);
      m_context.current_active.insert(m_context.execution_order_iter->vertex);
      if (!m_context.execution_order_iter->taken &&
          m_context.ops[m_context.execution_order_iter->vertex]->is_blocked())
        blocked++;
    }
    else
      retired++;
  }

  //HT_INFOF("current size = %lu, blocked = %lu", m_context.current.size(), blocked);

  m_context.current_iter = m_context.current.begin();
  while (m_context.current_iter != m_context.current.end() && m_context.current_iter->taken)
    ++m_context.current_iter;

  if (m_context.current_iter != m_context.current.end() ||
      blocked != m_context.current.size()) {
    m_context.cond.notify_all();
    return true;
  }
  return false;
}
