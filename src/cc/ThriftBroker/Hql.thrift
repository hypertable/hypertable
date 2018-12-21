/**
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file and its generated files are licensed under the Apache License,
 * Version 2.0 (the "License"); You may not use this file and its generated
 * files except in compliance with the License. You may obtain a copy of the
 * License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

include "Client.thrift"

/**
 * namespace for target languages
 */
namespace cpp   Hypertable.ThriftGen
namespace java  org.hypertable.thriftgen
namespace perl  Hypertable.ThriftGen2 # perl generator would overide types
namespace php   Hypertable_ThriftGen2 # ditto
namespace py    hypertable.thrift_client.hyperthrift.gen2
namespace rb    Hypertable.ThriftGen

/**
 * Result type of HQL queries
 *
 * <dl>
 *   <dt>results</dt>
 *   <dd>String results from metadata queries</dd>
 *
 *   <dt>cells</dt>
 *   <dd>Resulting table cells of for buffered queries</dd>
 *
 *   <dt>scanner</dt>
 *   <dd>Resulting scanner ID for unbuffered queries</dd>
 *
 *   <dt>mutator</dt>
 *   <dd>Resulting mutator ID for unflushed modifying queries</dd>
 * </dl>
 */
struct HqlResult {
  1: optional list<string> results,
  2: optional list<Client.Cell> cells,
  3: optional i64 scanner,
  4: optional i64 mutator
}

/**
 * Same as HqlResult except with cell as array
 */
struct HqlResult2 {
  1: optional list<string> results,
  2: optional list<Client.CellAsArray> cells,
  3: optional i64 scanner,
  4: optional i64 mutator
}

/**
 * Same as HqlResult except with cell as array
 */
struct HqlResultAsArrays {
  1: optional list<string> results,
  2: optional list<Client.CellAsArray> cells,
  3: optional i64 scanner,
  4: optional i64 mutator
}

/**
 * HQL service is a superset of Client service
 *
 * It adds capability to execute HQL queries to the service
 */
service HqlService extends Client.ClientService {

  /**
   * Execute an HQL command
   *
   * @param ns - Namespace id 
   *
   * @param command - HQL command
   *
   * @param noflush - Do not auto commit any modifications (return a mutator)
   *
   * @param unbuffered - return a scanner instead of buffered results
   */
  HqlResult hql_exec(1:i64 ns, 2:string command, 3:bool noflush = 0,
                     4:bool unbuffered = 0)
      throws (1:Client.ClientException e),

  /**
   * Convenience method for executing an buffered and flushed query
   *
   * because thrift doesn't (and probably won't) support default argument values
   *
   * @param ns - Namespace 
   * 
   * @param command - HQL command
   */
  HqlResult hql_query(1:i64 ns, 2:string command) throws (1:Client.ClientException e)

  /**
   * @see hql_exec
   */
  HqlResultAsArrays hql_exec_as_arrays(1:i64 ns, 2:string command, 3:bool noflush = 0,
                       4:bool unbuffered = 0)
      throws (1:Client.ClientException e),

  HqlResult2 hql_exec2(1:i64 ns, 2:string command, 3:bool noflush = 0,
                       4:bool unbuffered = 0)
      throws (1:Client.ClientException e),

  /**
   * @see hql_query
   */
  HqlResultAsArrays hql_query_as_arrays(1:i64 ns, 2:string command) throws (1:Client.ClientException e)

  HqlResult2 hql_query2(1:i64 ns, 2:string command) throws (1:Client.ClientException e)
}
