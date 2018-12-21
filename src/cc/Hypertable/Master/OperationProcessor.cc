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

/** @file
 * Definitions for OperationProcessor.
 * This file contains definitions for OperationProcessor, a class for executing
 * operations that have a dependency relationship in reverse topological
 * order.
 */

#include <Common/Compat.h>
#include "OperationProcessor.h"

#include <Hypertable/Master/OperationInitialize.h>

#include <Common/Path.h>
#include <Common/StringExt.h>

#include <boost/graph/topological_sort.hpp>
#include <boost/graph/graphviz.hpp>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unordered_map>

using namespace Hypertable;
using namespace boost;
using namespace std;

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

  if (context->props->get_bool("Hypertable.Master.RecordGraphvizStream")) {
    Path data_dir = Path(context->props->get_str("Hypertable.DataDirectory"));
    string run_dir = (data_dir /= "/run").string();
    string filename = run_dir + "/graphviz-stream";
    m_graphviz_out = std::make_unique<std::ofstream>(filename.c_str(), ofstream::out|ofstream::app);
  }

  m_context.execution_order_iter = m_context.execution_order.end();
  m_context.op = this;
  Worker worker(m_context);
  for (size_t i=0; i<thread_count; ++i)
    m_threads.create_thread(worker);
}

OperationProcessor::~OperationProcessor() {
  shutdown();
  join();
}


void OperationProcessor::add_operation(OperationPtr operation) {
  std::lock_guard<std::mutex> lock(m_context.mutex);

  //HT_INFOF("Adding operation %s", operation->label().c_str());

  // Make sure this operation hasn't already been added
  if (m_context.op_ids.count(operation->id()) > 0)
    return;

  if (!operation->is_complete() || operation->is_perpetual()) {
    add_operation_internal(operation);
    m_context.need_order_recompute = true;
    m_context.current_iter = m_context.current.end();
    m_context.cond.notify_all();
  }
  else if (operation->get_remove_approval_mask() == 0)
    m_context.master_context->response_manager->add_operation(operation);

}

void OperationProcessor::add_operations(std::vector<OperationPtr> &operations) {
  std::lock_guard<std::mutex> lock(m_context.mutex);
  bool added = false;

  for (auto & operation : operations) {

    //HT_INFOF("Adding operation %s", operations[i]->label().c_str());

    // Make sure this operation hasn't already been added
    if (m_context.op_ids.count(operation->id()) > 0)
      continue;

    if (!operation->is_complete() || operation->is_perpetual()) {
      add_operation_internal(operation);
      added = true;
    }
    else if (operation->get_remove_approval_mask() == 0)
      m_context.master_context->response_manager->add_operation(operation);
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
  m_context.operation_hash[operation->hash_code()]=OperationVertex(operation,v);
  add_dependencies(v, operation);
  HT_ASSERT(m_context.op_ids.insert(operation->id()).second);
}

OperationPtr OperationProcessor::remove_operation(int64_t hash_code) {
  std::lock_guard<std::mutex> lock(m_context.mutex);
  OperationPtr operation;
  Vertex vertex;

  //  std::unordered_map<int64_t, OperationVertex>::iterator iter =
  auto iter = m_context.operation_hash.find(hash_code);

  if (iter == m_context.operation_hash.end() ||
      m_context.busy[iter->second.vertex])
    return 0;

  operation = iter->second.operation;
  vertex = iter->second.vertex;

  retire_operation(vertex, operation);

  return operation;
}

void OperationProcessor::shutdown() {
  std::lock_guard<std::mutex> lock(m_context.mutex);
  m_context.shutdown = true;
  m_context.cond.notify_all();
}

void OperationProcessor::join() {
  m_threads.join_all();
}

void OperationProcessor::wait_for_empty() {
  std::unique_lock<std::mutex> lock(m_context.mutex);
  m_context.idle_cond.wait(lock, [this](){
      return num_vertices(m_context.graph) == 0; });
}

void OperationProcessor::wait_for_idle() {
  std::unique_lock<std::mutex> lock(m_context.mutex);
  while (m_context.busy_count > 0 ||
         m_context.need_order_recompute ||
         m_context.execution_order_iter != m_context.execution_order.end())
    m_context.idle_cond.wait(lock);
}

bool OperationProcessor::wait_for_idle(std::chrono::milliseconds max_wait) {
  std::unique_lock<std::mutex> lock(m_context.mutex);
  while (m_context.busy_count > 0 ||
         m_context.need_order_recompute ||
         m_context.execution_order_iter != m_context.execution_order.end()) {
    if (m_context.idle_cond.wait_for(lock, max_wait) == std::cv_status::timeout)
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
  std::lock_guard<std::mutex> lock(m_context.mutex);
  m_context.need_order_recompute = true;
  m_context.cond.notify_all();
}

void OperationProcessor::unblock(const String &name) {
  std::lock_guard<std::mutex> lock(m_context.mutex);
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

  if (unblocked_something) {
    m_context.current_blocked = 0;
    m_context.need_order_recompute = true;
    m_context.cond.notify_all();
  }

}

void OperationProcessor::activate(const String &name) {
  std::lock_guard<std::mutex> lock(m_context.mutex);

  if (!m_context.perpetual_ops.empty()) {
    DependencySet names;
    PerpetualSet::iterator iter = m_context.perpetual_ops.begin();
    OperationPtr operation;
    while (iter != m_context.perpetual_ops.end()) {
      (*iter)->obstructions(names);
#if 0      
      {
        String str;
        for (const auto &tag : names)
          str += tag + " ";
        HT_INFOF("Activating %s with obstructions %s", (*iter)->label().c_str(), str.c_str());
      }
#endif
      if (names.count(name) > 0) {
        PerpetualSet::iterator rm_iter = iter;
        operation = *iter++;
        m_context.perpetual_ops.erase(rm_iter);
        operation->set_state(OperationState::INITIAL);
        add_operation_internal(operation);
        m_context.need_order_recompute = true;
        m_context.current_iter = m_context.current.end();
        m_context.cond.notify_all();
      }
      else
        ++iter;
    }
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
        std::unique_lock<std::mutex> lock(m_context.mutex);

        if (m_context.shutdown)
          return;

        while (m_context.current_iter == m_context.current.end()) {

          if (m_context.need_order_recompute) {
            m_context.op->recompute_order();
            current_needs_loading = true;
          }
          else
            current_needs_loading =
              m_context.current_active.empty() && m_context.current_blocked == 0;

          if (current_needs_loading &&
              m_context.execution_order_iter != m_context.execution_order.end()) {
            if (m_context.op->load_current()) {
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

        operation->pre_run();
        if (!operation->is_blocked())
          operation->execute();
        operation->post_run();

        {
          std::lock_guard<std::mutex> lock(m_context.mutex);
          m_context.busy[vertex] = false;
          m_context.busy_count--;
          m_context.current_active.erase(vertex);
          if (operation->is_complete())
            m_context.op->retire_operation(vertex, operation);
          else if (operation->is_blocked())
            m_context.current_blocked++;
          else
            m_context.op->update_operation(vertex, operation);
        }
      }
      catch (Exception &e) {
        std::lock_guard<std::mutex> lock(m_context.mutex);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
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
        PerpetualSet::iterator rm_iter = iter;
        operation = *iter++;
        m_context.perpetual_ops.erase(rm_iter);
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
  std::lock_guard<std::mutex> lock(m_context.mutex);
  std::ostringstream oss;
  write_graphviz(oss, m_context.graph, make_label_writer(m_context.label));
  output = oss.str();
}

void OperationProcessor::state_description(String &output) {
  std::lock_guard<std::mutex> lock(m_context.mutex);
  std::ostringstream oss;
  DependencySet names;

  oss << "Num vertices = " << num_vertices(m_context.graph) << "\n";
  oss << "Busy count = " << m_context.busy_count << "\n";
  oss << "Active set size = " << m_context.current_active.size() << "\n";
  oss << "Need order recompute = " << (m_context.need_order_recompute ? "true\n" : "false\n");
  oss << "Shutdown = " << (m_context.shutdown ? "true\n" : "false\n");
  oss << "Paused = " << (m_context.paused ? "true\n" : "false\n");
  oss << "\n";

  std::pair<GraphTraits::vertex_iterator, GraphTraits::vertex_iterator> vp;
  std::set<Vertex> vset;
  size_t i = 0;
  bool first;
  for (vp = vertices(m_context.graph); vp.first != vp.second; ++vp.first) {
    vset.insert(*vp.first);
    oss << i << ": " << m_context.ops[*vp.first]->label() << "\n";
    oss << "  active: " << ((m_context.current_active.count(*vp.first) > 0) ? "true\n" : "false\n");
    oss << "  live: " << ((m_context.live.count(*vp.first) > 0) ? "true\n" : "false\n");
    oss << "  exclusive: " << (m_context.ops[*vp.first]->exclusive() ? "true\n" : "false\n");
    oss << "  perpetual: " << (m_context.ops[*vp.first]->is_perpetual() ? "true\n" : "false\n");
    oss << "  blocked: " << (m_context.ops[*vp.first]->is_blocked() ? "true\n" : "false\n");
    oss << "  state: " << OperationState::get_text(m_context.ops[*vp.first]->get_state()) << "\n";
    oss << "  dependencies: (";
    first = true;
    names.clear();
    m_context.ops[*vp.first]->dependencies(names);
    for (const auto &str : names) {
      if (!first)
        oss << ",";
      oss << str;
      first = false;
    }
    oss << ")\n";
    oss << "  obstructions: (";
    first = true;
    names.clear();
    m_context.ops[*vp.first]->obstructions(names);
    for (const auto &str : names) {
      if (!first)
        oss << ",";
      oss << str;
      first = false;
    }
    oss << ")\n";
    oss << "  exclusivities: (";
    first = true;
    names.clear();
    m_context.ops[*vp.first]->exclusivities(names);
    for (const auto &str : names) {
      if (!first)
        oss << ",";
      oss << str;
      first = false;
    }
    oss << ")\n";
    oss << "\n";
  }
  
  oss << "Current:\n";
  for (ExecutionList::iterator iter = m_context.current.begin();
       iter != m_context.current.end(); ++iter) {
    if (iter == m_context.current_iter)
      oss << "*";
    if (vset.count(iter->vertex))
      oss << m_context.ops[iter->vertex]->label() << "\n";
    else
      oss << "[retired]\n" ;
  }
  if (m_context.current_iter == m_context.current.end())
    oss << "*\n";
  oss << "\n";

  oss << "Execution order:\n";
  for (ExecutionList::iterator iter = m_context.execution_order.begin();
       iter != m_context.execution_order.end(); ++iter) {
    if (iter == m_context.execution_order_iter)
      oss << "*";
    if (vset.count(iter->vertex)) {
      oss << m_context.ops[iter->vertex]->label() << " (time=";
      oss << m_context.exec_time[iter->vertex] << ")\n";
    }
    else
      oss << "[retired]\n" ;
  }
  if (m_context.execution_order_iter == m_context.execution_order.end())
    oss << "*\n";
  oss << "\n";

  oss << "Graphviz:\n";
  write_graphviz(oss, m_context.graph, make_label_writer(m_context.label));
  output = oss.str();
}


void OperationProcessor::retire_operation(Vertex v, OperationPtr &operation) {
  m_context.op->purge_from_obstruction_index(v);
  m_context.op->purge_from_dependency_index(v);
  m_context.op->purge_from_exclusivity_index(v);
  if (in_degree(v, m_context.graph) > 0)
    m_context.need_order_recompute = true;
  clear_vertex(v, m_context.graph);
  remove_vertex(v, m_context.graph);
  m_context.live.erase(v);
  m_context.operation_hash.erase(operation->hash_code());
  if (operation->exclusive())
    m_context.exclusive_ops.erase(operation->name());
  //HT_INFOF("Retiring op %p vertex %p", operation.get(), v);

  if (m_context.master_context->metrics_handler)
    m_context.master_context->metrics_handler->operation_increment();

  if (operation->is_perpetual())
    m_context.perpetual_ops.insert(operation);
  else {
    if (operation->get_remove_approval_mask() == 0 &&
        operation->get_state() == OperationState::COMPLETE)
      m_context.master_context->response_manager->add_operation(operation);
  }
  m_context.op_ids.erase(operation->id());
}


void OperationProcessor::update_operation(Vertex v, OperationPtr &operation) {
  not_permanent np(m_context);

  m_context.op->purge_from_obstruction_index(v);
  m_context.op->purge_from_dependency_index(v);

  remove_in_edge_if(v, np, m_context.graph);
  remove_out_edge_if(v, np, m_context.graph);

  m_context.op->add_dependencies(v, operation);

  // Add sub-operations
  std::vector<OperationPtr> sub_ops;
  operation->fetch_sub_operations(sub_ops);
  for (auto op : sub_ops) {
    if (m_context.op_ids.count(op->id()) == 0 && !op->is_complete()) {
      m_context.op->add_operation_internal(op);
    }
  }

  m_context.need_order_recompute = true;
  m_context.current_iter = m_context.current.end();
  m_context.cond.notify_all();
}


void OperationProcessor::recompute_order() {

  // re-assign vertex indexes
  property_map<OperationGraph, vertex_index_t>::type index = get(vertex_index, m_context.graph);
  int i=0;
  std::pair<GraphTraits::vertex_iterator, GraphTraits::vertex_iterator> vp;
  for (vp = vertices(m_context.graph); vp.first != vp.second; ++vp.first)
    put(index, *vp.first, i++);

  if (m_graphviz_out) {
    write_graphviz(*m_graphviz_out, m_context.graph, make_label_writer(m_context.label));
    *m_graphviz_out << flush;
  }

  m_context.execution_order.clear();
  try {
    topological_sort(m_context.graph, std::back_inserter(m_context.execution_order));
  }
  catch (std::invalid_argument &e) {
    write_graphviz(std::cout, m_context.graph, make_label_writer(m_context.label));
  }

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


bool OperationProcessor::load_current() {
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

