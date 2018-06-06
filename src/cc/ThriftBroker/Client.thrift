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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

/** Namespace for generated types */
namespace cpp   Hypertable.ThriftGen
namespace java  org.hypertable.thriftgen
namespace perl  Hypertable.ThriftGen
namespace php   Hypertable_ThriftGen
# python doesn't like multiple top dirs with the same name (say hypertable)
namespace py    hypertable.thrift_client.hyperthrift.gen
namespace rb    Hypertable.ThriftGen

/** Opaque ID for a Future object 
 *
 */
typedef i64 Future 


/** Opaque ID for a Namespace 
 *
 */
typedef i64 Namespace 

/** Opaque ID for a table scanner
 *
 * A scanner is recommended for returning large amount of data, say a full
 * table dump.
 */
typedef i64 Scanner

/** Opaque ID for a asynchronous table scanner
 *
 * A scanner is recommended for returning large amount of data, say a full
 * table dump.
 */
typedef i64 ScannerAsync


/** Opaque ID for a table mutator
 *
 * A mutator is recommended for injecting large amount of data (across
 * many calls to mutator methods)
 */
typedef i64 Mutator

/** Opaque ID for a asynchronous table mutator 
 *
 * A mutator is recommended for injecting large amount of data (across
 * many calls to mutator methods)
 */
typedef i64 MutatorAsync  


/** Value for table cell
 *
 * Use binary instead of string to generate efficient type for Java.
 */
typedef binary Value

/** Specifies a range of rows
 *
 * <dl>
 *   <dt>start_row</dt>
 *   <dd>The row to start scan with. Must not contain nulls (0x00)</dd>
 *
 *   <dt>start_inclusive</dt>
 *   <dd>Whether the start row is included in the result (default: true)</dd>
 *
 *   <dt>end_row</dt>
 *   <dd>The row to end scan with. Must not contain nulls</dd>
 *
 *   <dt>end_inclusive</dt>
 *   <dd>Whether the end row is included in the result (default: true)</dd>
 * </dl>
 */
struct RowInterval {
  1: optional string start_row
  2: optional bool start_inclusive = 1
  3: optional string end_row    # 'end' chokes ruby
  4: optional bool end_inclusive = 1
  5: optional binary start_row_binary
  6: optional binary end_row_binary
}

/** Specifies a range of cells
 *
 * <dl>
 *   <dt>start_row</dt>
 *   <dd>The row to start scan with. Must not contain nulls (0x00)</dd>
 *
 *   <dt>start_column</dt>
 *   <dd>The column (prefix of column_family:column_qualifier) of the
 *   start row for the scan</dd>
 *
 *   <dt>start_inclusive</dt>
 *   <dd>Whether the start row is included in the result (default: true)</dd>
 *
 *   <dt>end_row</dt>
 *   <dd>The row to end scan with. Must not contain nulls</dd>
 *
 *   <dt>end_column</dt>
 *   <dd>The column (prefix of column_family:column_qualifier) of the
 *   end row for the scan</dd>
 *
 *   <dt>end_inclusive</dt>
 *   <dd>Whether the end row is included in the result (default: true)</dd>
 * </dl>
 */
struct CellInterval {
  1: optional string start_row
  2: optional string start_column
  3: optional bool start_inclusive = 1
  4: optional string end_row
  5: optional string end_column
  6: optional bool end_inclusive = 1
}

/** The "operation" for a ColumnPredicate
 *
 *  EXACT_MATCH: compares the cell value for identity 
 *      (... WHERE column = "value")
 *  PREFIX_MATCH: compares the cell value for a prefix match 
 *      (... WHERE column =^ "prefix")
 */
enum ColumnPredicateOperation {
  EXACT_MATCH  = 1,
  PREFIX_MATCH = 2,
  REGEX_MATCH  = 4,
  VALUE_MATCH  = 7,
  QUALIFIER_EXACT_MATCH  = 256,
  QUALIFIER_PREFIX_MATCH = 512,
  QUALIFIER_REGEX_MATCH  = 1024,
  QUALIFIER_MATCH        = 1792
}

/** Specifies a column predicate
 *     ... WHERE column = "value"
 *   or
 *     ... WHERE column =^ "prefix"
 *
 * <dl>
 *   <dt>column_family</dt>
 *   <dd>The name of the column family</dd>
 *
 *   <dt>operation</dt>
 *   <dd>The predicate operation; either EXACT_MATCH or PREFIX_MATCH</dd>
 *
 *   <dt>value</dt>
 *   <dd>The cell value or cell prefix, depending on the operation</dd>
 *
 *   <dt>value_len</dt>
 *   <dd>The size of the value</dd>
 *
 *   <dt>column_qualifier</dt>
 *   <dd>The column qualifier</dd>
 * </dl>
 */
struct ColumnPredicate {
  1: optional string column_family
  2: optional string column_qualifier
  3: i32 operation
  4: optional string value
}

/** Specifies options for a scan
 *
 * <dl>
 *   <dt>row_intervals</dt>
 *   <dd>A list of ranges of rows to scan. Mutually exclusive with
 *   cell_interval</dd>
 *
 *   <dt>cell_intervals</dt>
 *   <dd>A list of ranges of cells to scan. Mutually exclusive with
 *   row_intervals</dd>
 *
 *   <dt>return_deletes</dt>
 *   <dd>Indicates whether cells pending delete are returned</dd>
 *
 *   <dt>revs</dt>
 *   <dd>Specifies max number of revisions of cells to return</dd>
 *
 *   <dt>row_limit</dt>
 *   <dd>Specifies max number of rows to return</dd>
 *
 *   <dt>start_time</dt>
 *   <dd>Specifies start time in nanoseconds since epoch for cells to
 *   return</dd>
 *
 *   <dt>end_time</dt>
 *   <dd>Specifies end time in nanoseconds since epoch for cells to return</dd>
 *
 *   <dt>columns</dt>
 *   <dd>Specifies the names of the columns to return</dd>
 *
 *   <dt>cell_limit</dt>
 *   <dd>Specifies max number of cells to return</dd>
 *
 *   <dt>cell_limit_per_family</dt>
 *   <dd>Specifies max number of cells to return per column family</dd>
 *
 *   <dt>row_regexp</dt>
 *   <dd>Specifies a regexp used to filter by rowkey</dd>
 *
 *   <dt>value_regexp</dt>
 *   <dd>Specifies a regexp used to filter by cell value</dd>
 *
 *   <dt>scan_and_filter_rows</dt>
 *   <dd>Indicates whether table scan filters the rows specified instead of individual look up</dd>
 *
 *   <dt>row_offset</dt>
 *   <dd>Specifies number of rows to be skipped</dd>
 *
 *   <dt>cell_offset</dt>
 *   <dd>Specifies number of cells to be skipped</dd>
 * </dl>
 */
struct ScanSpec {
  1: optional list<RowInterval> row_intervals
  2: optional list<CellInterval> cell_intervals
  3: optional bool return_deletes = 0
  4: optional i32 versions = 0
  5: optional i32 row_limit = 0
  6: optional i64 start_time
  7: optional i64 end_time
  8: optional list<string> columns
  9: optional bool keys_only = 0
  14:optional i32 cell_limit = 0 
  10:optional i32 cell_limit_per_family = 0 
  11:optional string row_regexp
  12:optional string value_regexp
  13:optional bool scan_and_filter_rows = 0
  15:optional i32 row_offset = 0
  16:optional i32 cell_offset = 0 
  17:optional list<ColumnPredicate> column_predicates
  18:optional bool do_not_cache = 0
  19:optional bool and_column_predicates = 0
}


/** State flags for a key
 *
 * Note for maintainers: the definition must be sync'ed with FLAG_* constants
 * in src/cc/Hypertable/Lib/Key.h
 *
 * DELETE_ROW: row is pending delete
 *
 * DELETE_CF: column family is pending delete
 *
 * DELETE_CELL: key is pending delete
 *
 * DELETE_CELL_VERSION: delete specific timestamped version of key
 *
 * INSERT: key is an insert/update (default state)
 */
enum KeyFlag {
  DELETE_ROW = 0,
  DELETE_CF = 1,
  DELETE_CELL = 2,
  DELETE_CELL_VERSION = 3,
  INSERT = 255
}

/**
 * Defines a cell key
 *
 * <dl>
 *   <dt>row</dt>
 *   <dd>Specifies the row key. Note, it cannot contain null characters.
 *   If a row key is not specified in a return cell, it's assumed to
 *   be the same as the previous cell</dd>
 *
 *   <dt>column_family</dt>
 *   <dd>Specifies the column family</dd>
 *
 *   <dt>column_qualifier</dt>
 *   <dd>Specifies the column qualifier. A column family must be specified.</dd>
 *
 *   <dt>timestamp</dt>
 *   <dd>Nanoseconds since epoch for the cell<dd>
 *
 *   <dt>revision</dt>
 *   <dd>A 64-bit revision number for the cell</dd>
 *
 *   <dt>flag</dt>
 *   <dd>A 16-bit integer indicating the state of the cell</dd>
 * </dl>
 */
struct Key {
  1: string row
  2: string column_family
  3: string column_qualifier
  4: optional i64 timestamp
  5: optional i64 revision
  6: KeyFlag flag = KeyFlag.INSERT
}


/** Mutator creation flags
 *
 * NO_LOG_SYNC: Do not sync the commit log
 * IGNORE_UNKNOWN_CFS: Don't throw exception if mutator writes to unknown column family
 * NO_LOG: Don't write to the commit log
 */
enum MutatorFlag {
  NO_LOG_SYNC = 1,
  IGNORE_UNKNOWN_CFS = 2,
  NO_LOG = 4
}

/** Specifies options for a shared periodic mutator 
 *
 * <dl>
 *   <dt>appname</dt>
 *   <dd>String key used to share/retrieve mutator, eg: "my_ht_app"</dd> 
 *
 *   <dt>flush_interval</dt>
 *   <dd>Time interval between flushes</dd>
 *
 *   <dt>flags</dt>
 *   <dd>Mutator flags</dt>
 * </dl>
 */
struct MutateSpec {
  1: required string appname = ""
  2: required i32 flush_interval = 1000
  3: required i32 flags = MutatorFlag.IGNORE_UNKNOWN_CFS 
}

/**
 * Defines a table cell
 *
 * <dl>
 *   <dt>key</dt>
 *   <dd>Specifies the cell key</dd>
 *
 *   <dt>value</dt>
 *   <dd>Value of a cell. Currently a sequence of uninterpreted bytes.</dd>
 * </dl>
 */
struct Cell {
  1: Key key
  2: optional Value value
}

/**
 * Alternative Cell interface for languages (e.g., Ruby) where user defined
 * objects are much more expensive to create than builtin primitives. The order
 * of members is the same as that in the <a href="#Struct_Cell">Cell</a> object
 * definition.
 *
 * The returned cells (as arrays) contain Cell as array:
 * ["row_key", "column_family", "column_qualifier", "value", "timestamp"]
 *
 * Note, revision and cell flag are not returned for the array interface.
 */
typedef list<string> CellAsArray

/**
 * Binary buffer holding serialized sequence of cells
 */
typedef binary CellsSerialized

/** Specifies a result object for asynchronous requests.
 * TODO: add support for update results
 *
 * <dl>
 *   <dt>is_empty</dt>
 *   <dd>Indicates whether this object contains a result or not</dd>
 * 
 *   <dt>id</dt>
 *   <dd>Scanner/mutator id for which these results pertain to</dd>
 *   
 *   <dt>is_scan</dt>
 *   <dd>Indicates whether these are scan results or update results</dd>
 *   
 *   <dt>is_error</dt>
 *   <dd>Indicates whether the async request was successful or not</dd>
 *   
 *   <dt>error</dt>
 *   <dd>Error code</dd>
 *
 *   <dt>error_msg</dt>
 *   <dd>Error message</dd>
 * 
 *   <dt>cells</dt>
 *   <dd>Cells returned by asynchronous scanner</dd>
 * </dl>
 */
struct Result {
  1: required bool is_empty
  2: required i64 id 
  3: required bool is_scan
  4: required bool is_error
  5: optional i32 error
  6: optional string error_msg
  7: optional list<Cell> cells
}

/** Specifies a result object for asynchronous requests.
 * TODO: add support for update results
 *
 * <dl>
 *   <dt>is_empty</dt>
 *   <dd>Indicates whether this object contains a result or not</dd>
 * 
 *   <dt>id</dt>
 *   <dd>Scanner/mutator id for which these results pertain to</dd>
 *   
 *   <dt>is_scan</dt>
 *   <dd>Indicates whether these are scan results or update results</dd>
 *   
 *   <dt>is_error</dt>
 *   <dd>Indicates whether the async request was successful or not</dd>
 *   
 *   <dt>error</dt>
 *   <dd>Error code</dd>
 *
 *   <dt>error_msg</dt>
 *   <dd>Error message</dd>
 * 
 *   <dt>cells</dt>
 *   <dd>Cells returned by asynchronous scanner</dd>
 * </dl>
 */
struct ResultAsArrays {
  1: required bool is_empty
  2: required i64 id 
  3: required bool is_scan
  4: required bool is_error
  5: optional i32 error
  6: optional string error_msg
  7: optional list<CellAsArray> cells
}

/** Specifies a serialized result object for asynchronous requests.
 * TODO: add support for update results
 *
 * <dl>
 *   <dt>is_empty</dt>
 *   <dd>Indicates whether this object contains a result or not</dd>
 * 
 *   <dt>id</dt>
 *   <dd>Scanner/mutator id for which these results pertain to</dd>
 *   
 *   <dt>is_scan</dt>
 *   <dd>Indicates whether these are scan results or update results</dd>
 *   
 *   <dt>is_error</dt>
 *   <dd>Indicates whether the async request was successful or not</dd>
 *   
 *   <dt>error</dt>
 *   <dd>Error code</dd>
 *
 *   <dt>error_msg</dt>
 *   <dd>Error message</dd>
 * 
 *   <dt>cells</dt>
 *   <dd>Cells returned by asynchronous scanner</dd>
 * </dl>
 */
struct ResultSerialized {
  1: required bool is_empty
  2: required i64 id 
  3: required bool is_scan
  4: required bool is_error
  5: optional i32 error
  6: optional string error_msg
  7: optional CellsSerialized cells
}

/**
 * Defines an individual namespace listing 
 *
 * <dl>
 *   <dt>name</dt>
 *   <dd>Name of the listing.</dd>
 *
 *   <dt>is_namespace</dt>
 *   <dd>true if this entry is a namespace.</dd>
 * </dl>
 */
struct NamespaceListing {
  1: required string name 
  2: required bool is_namespace 
}
/**
 * Defines a table split
 *
 * <dl>
 *   <dt>start_row</dt>
 *   <dd>Starting row of the split.</dd>
 *
 *   <dt>end_row</dt>
 *   <dd>Ending row of the split.</dd>
 *
 *   <dt>location</dt>
 *   <dd>Location (proxy name) of the split.</dd>
 *
 *   <dt>ip_address</dt>
 *   <dd>The IP address of the split.</dd>
 *
 *   <dt>hostname</dt>
 *   <dd>The hostname of the split.</dd>
 * </dl>
 */
struct TableSplit {
  1: optional binary start_row
  2: optional binary end_row
  3: optional string location
  4: optional string ip_address
  5: optional string hostname
}

struct ColumnFamilyOptions {
  1: optional i32 max_versions 
  2: optional i32 ttl 
  3: optional bool time_order_desc
  4: optional bool counter
}


/**  
 * Describes a ColumnFamily 
 * <dl>
 *   <dt>name</dt>
 *   <dd>Name of the column family</dd>
 *
 *   <dt>ag</dt>
 *   <dd>Name of the access group for this CF</dd>
 *
 *   <dt>max_versions</dt>
 *   <dd>Max versions of the same cell to be stored</dd> 
 *
 *   <dt>ttl</dt>
 *   <dd>Time to live for cells in the CF (ie delete cells older than this time)</dd> 
 * </dl>
 */
struct ColumnFamilySpec {
  1: string name 
  2: string access_group
  3: bool deleted
  4: optional i64 generation
  5: optional i32 id
  6: bool value_index
  7: bool qualifier_index
  8: optional ColumnFamilyOptions options
}

struct AccessGroupOptions {
  1: optional i16 replication
  2: optional i32 blocksize
  3: optional string compressor
  4: optional string bloom_filter
  5: optional bool in_memory
}

/**  
 * Describes an AccessGroup 
 * <dl>
 *   <dt>name</dt>
 *   <dd>Name of the access group</dd>
 *
 *   <dt>in_memory</dt>
 *   <dd>Is this access group in memory</dd>
 *
 *   <dt>replication</dt>
 *   <dd>Replication factor for this AG</dd> 
 *
 *   <dt>blocksize</dt>
 *   <dd>Specifies blocksize for this AG</dd> 
 *
 *   <dt>compressor</dt>
 *   <dd>Specifies compressor for this AG</dd>
 *
 *   <dt>bloom_filter</dt>
 *   <dd>Specifies bloom filter type</dd>
 *
 *   <dt>columns</dt>
 *   <dd>Specifies list of column families in this AG</dd> 
 * </dl>
 */
struct AccessGroupSpec {
  1: string name
  2: optional i64 generation
  3: optional AccessGroupOptions options
  4: optional ColumnFamilyOptions defaults
}

/**  
 * Describes a schema.
 * <dl>
 *   <dt>access_groups</dt>
 *   <dd>Map of access groups</dd>
 *
 *   <dt>column_families</dt>
 *   <dd>Map of column families</dd>
 * </dl>
 */
struct Schema {
  1: optional map<string, AccessGroupSpec> access_groups
  2: optional map<string, ColumnFamilySpec> column_families
  3: optional i64 generation
  4: optional i32 version
  5: optional i32 group_commit_interval
  6: optional AccessGroupOptions access_group_defaults
  7: optional ColumnFamilyOptions column_family_defaults
}


/** Status codes.
 */
enum StatusCode {
  OK = 0,
  WARNING = 1,
  CRITICAL = 2,
  UNKNOWN = 3
}

/** Status information.
 *
 * <dl>
 *   <dt>code</dt>
 *   <dd>Status code</dd>
 *
 *   <dt>text</dt>
 *   <dd>Status text</dd>
 * </dl>
 */
struct Status {
  1: required i32 code = StatusCode.OK
  2: required string text
}


/**
 * Exception for thrift clients.
 *
 * <dl>
 *   <dt>code</dt><dd>Internal use (defined in src/cc/Common/Error.h)</dd>
 *   <dt>message</dt><dd>A message about the exception</dd>
 * </dl>
 *
 * Note: some languages (like php) don't have adequate namespace, so Exception
 * would conflict with language builtins.
 */
exception ClientException {
  1: i32 code
  2: string message
}

/**
 * The client service mimics the C++ client API, with table, scanner and
 * mutator interface flattened.
 */
service ClientService {
  /**
   * Create a namespace 
   *
   * @param ns - namespace name 
   */
  void namespace_create(1:string ns) throws (1:ClientException e),
  void create_namespace(1:string ns) throws (1:ClientException e),

  /**
   * Create a table
   *
   * @param ns - namespace id 
   * @param table_name - table name
   * @param schema - schema of the table
   */
  void table_create(1:Namespace ns, 2:string table_name, 3:Schema schema)
      throws (1:ClientException e),
  
  /**
   * Alter a table
   *
   * @param ns - namespace id 
   * @param table_name - table name
   * @param schema - schema of the table
   */
  void table_alter(1:Namespace ns, 2:string table_name, 3:Schema schema)
      throws (1:ClientException e),
  
  /**
   * Refreshes the cached table information
   *
   * @param ns - namespace
   * @param table_name - name of the table
   */
  void refresh_table(1:Namespace ns, 2:string table_name)
      throws (1:ClientException e),
  
  /**
   * Open a namespace 
   *
   * @param ns - namespace
   * @return value is guaranteed to be non-zero and unique
   */
  Namespace namespace_open(1:string ns) throws (1:ClientException e),
  Namespace open_namespace(1:string ns) throws (1:ClientException e),

  /**
   * Close a namespace 
   *
   * @param ns - namespace
   */
  void namespace_close(1:Namespace ns) throws (1:ClientException e),
  void close_namespace(1:Namespace ns) throws (1:ClientException e),

  /**
   * Open a future object 
   * @param capacity - Amount of result data the future object can enqueue without blocking threads  
   */
  Future future_open(1:i32 capacity = 0)
      throws (1:ClientException e),
  Future open_future(1:i32 capacity = 0)
      throws (1:ClientException e),
  
  /**
   * Cancel tasks outstanding in a future object 
   * @param ff - Future object 
   */
  void future_cancel(1:Future ff)
      throws (1:ClientException e),
  void cancel_future(1:Future ff)
      throws (1:ClientException e),
  
  /**
   * Fetch asynchronous results 
   * @param ff - Future object which has the asynchronous results
   * @return - result from async scanner/mutator
   */
  Result future_get_result(1:Future ff, 2:i32 timeout_millis = 0) throws (1:ClientException e),
  Result get_future_result(1:Future ff, 2:i32 timeout_millis = 0) throws (1:ClientException e),
  
  /**
   * Fetch asynchronous results 
   * @param ff - Future object which has the asynchronous results
   * @return - result from async scanner/mutator
   */
  ResultAsArrays future_get_result_as_arrays(1:Future ff, 2:i32 timeout_millis = 0) throws (1:ClientException e),
  ResultAsArrays get_future_result_as_arrays(1:Future ff, 2:i32 timeout_millis = 0) throws (1:ClientException e),
  
  /**
   * Fetch asynchronous results 
   * @param ff - Future object which has the asynchronous results
   * @return - result from async scanner/mutator
   */
  ResultSerialized future_get_result_serialized(1:Future ff, 2:i32 timeout_millis = 0) throws (1:ClientException e),
  ResultSerialized get_future_result_serialized(1:Future ff, 2:i32 timeout_millis = 0) throws (1:ClientException e),
  
  /**
   * Check if future object's queue is empty
   */
  bool future_is_empty(1: Future ff) throws (1:ClientException e),

  /**
   * Check if future object's queue is full
   */
  bool future_is_full(1: Future ff) throws (1:ClientException e),

  /**
   * Check if future object has been cancelled
   */
  bool future_is_cancelled(1: Future ff) throws (1:ClientException e),
  
  /**
   * Check if future object has outstanding operations
   */
  bool future_has_outstanding(1: Future ff) throws (1:ClientException e),

  /**
   * Close a future object 
   * @param ff - the future object to be closed
   */
  void future_close(1:Future ff)
      throws (1:ClientException e),
  void close_future(1:Future ff)
      throws (1:ClientException e),

  /**
   * Open a table scanner
   * @param ns - namespace id 
   * @param table_name - table name
   * @param scan_spec - scan specification
   */
  Scanner scanner_open(1:Namespace ns, 2:string table_name, 3:ScanSpec scan_spec)
      throws (1:ClientException e),
  Scanner open_scanner(1:Namespace ns, 2:string table_name, 3:ScanSpec scan_spec)
      throws (1:ClientException e),
  
  /**
   * Open an asynchronous table scanner
   * @param ns - namespace id 
   * @param table_name - table name
   * @param future - callback object
   * @param scan_spec - scan specification
   */
  ScannerAsync async_scanner_open(1:Namespace ns, 2:string table_name, 3:Future future, 
                                  4:ScanSpec scan_spec)
      throws (1:ClientException e),
  ScannerAsync open_scanner_async(1:Namespace ns, 2:string table_name, 3:Future future, 
                                  4:ScanSpec scan_spec)
      throws (1:ClientException e),


  /**
   * Close a table scanner
   *
   * @param scanner - scanner id to close
   */
  void scanner_close(1:Scanner scanner) throws (1:ClientException e),
  void close_scanner(1:Scanner scanner) throws (1:ClientException e),
  
  /**
   * Cancel a table scanner
   *
   * @param scanner - scanner id to close
   */
  void async_scanner_cancel(1:ScannerAsync scanner) throws (1:ClientException e),
  void cancel_scanner_async(1:ScannerAsync scanner) throws (1:ClientException e),

  /**
   * Close a table scanner
   *
   * @param scanner - scanner id to close
   */
  void async_scanner_close(1:ScannerAsync scanner) throws (1:ClientException e),
  void close_scanner_async(1:ScannerAsync scanner) throws (1:ClientException e),

  /**
   * Iterate over cells of a scanner
   *
   * @param scanner - scanner id
   */
  list<Cell> scanner_get_cells(1:Scanner scanner) throws (1:ClientException e),
  list<Cell> next_cells(1:Scanner scanner) throws (1:ClientException e),

  list<CellAsArray> scanner_get_cells_as_arrays(1:Scanner scanner)
      throws (1:ClientException e),
  list<CellAsArray> next_cells_as_arrays(1:Scanner scanner)
      throws (1:ClientException e),

  /**
   * Alternative interface returning buffer of serialized cells
   */
  CellsSerialized scanner_get_cells_serialized(1:Scanner scanner) throws (1:ClientException e),
  CellsSerialized next_cells_serialized(1:Scanner scanner) throws (1:ClientException e),

  /**
   * Iterate over rows of a scanner
   *
   * @param scanner - scanner id
   */
  list<Cell> scanner_get_row(1:Scanner scanner) throws (1:ClientException e),
  list<Cell> next_row(1:Scanner scanner) throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  list<CellAsArray> scanner_get_row_as_arrays(1:Scanner scanner)
      throws (1:ClientException e),
  list<CellAsArray> next_row_as_arrays(1:Scanner scanner)
      throws (1:ClientException e),

  /**
   * Alternate interface returning a buffer of serialized cells for iterating by row 
   * for a given scanner
   *
   * @param scanner - scanner id
   */
  CellsSerialized scanner_get_row_serialized(1:Scanner scanner) throws (1:ClientException e),
  CellsSerialized next_row_serialized(1:Scanner scanner) throws (1:ClientException e),

  /**
   * Get a row (convenience method for random access a row)
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @param row - row key
   *
   * @return a list of cells (with row_keys unset)
   */
  list<Cell> get_row(1:Namespace ns, 2:string table_name, 3:string row) throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  list<CellAsArray> get_row_as_arrays(1:Namespace ns, 2:string name, 3:string row)
      throws (1:ClientException e),

  /**
   * Alternative interface returning buffer of serialized cells
   */
  CellsSerialized get_row_serialized(1:Namespace ns, 2:string table_name, 3:string row)
      throws (1:ClientException e),


  /**
   * Get a cell (convenience method for random access a cell)
   *
   * @param ns - namespace id
   *
   * @param table_name - table name
   *
   * @param row - row key
   *
   * @param column - column name
   *
   * @return value (byte sequence)
   */
  Value get_cell(1:Namespace ns, 2:string table_name, 3:string row, 4:string column)
      throws (1:ClientException e),

  /**
   * Get cells (convenience method for access small amount of cells)
   *
   * @param ns - namespace id
   *  
   * @param table_name - table name
   *
   * @param scan_spec - scan specification
   *
   * @return a list of cells (a cell with no row key set is assumed to have
   *         the same row key as the previous cell)
   */
  list<Cell> get_cells(1:Namespace ns, 2:string table_name, 3:ScanSpec scan_spec)
      throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  list<CellAsArray> get_cells_as_arrays(1:Namespace ns, 2:string name, 3:ScanSpec scan_spec)
      throws (1:ClientException e),

  /**
   * Alternative interface returning buffer of serialized cells
   */
  CellsSerialized get_cells_serialized(1:Namespace ns, 2:string name, 3:ScanSpec scan_spec)
      throws (1:ClientException e),


  /**
   * Create a shared mutator with specified MutateSpec.
   * Delete and recreate it if the mutator exists.
   *
   * @param ns - namespace id
   *  
   * @param table_name - table name
   *
   * @param mutate_spec - mutator specification
   *
   */
  void shared_mutator_refresh(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec) 
      throws (1:ClientException e),
  void refresh_shared_mutator(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec) 
      throws (1:ClientException e),

  /**
   * Open a shared periodic mutator which causes cells to be written asyncronously. 
   * Users beware: calling this method merely writes
   * cells to a local buffer and does not guarantee that the cells have been persisted.
   * If you want guaranteed durability, use the open_mutator+set_cells* interface instead.
   *
   * @param ns - namespace id
   *
   * @param table_name - table name
   *
   * @param mutate_spec - mutator specification
   *
   * @param cells - set of cells to be written 
   */
  void shared_mutator_set_cells(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 4:list<Cell> cells)
      throws (1:ClientException e),
  void offer_cells(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 4:list<Cell> cells)
      throws (1:ClientException e),
  
  /**
   * Alternative to offer_cell interface using array as cell
   */
  void shared_mutator_set_cells_as_arrays(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 
                           4:list<CellAsArray> cells) throws (1:ClientException e),
  void offer_cells_as_arrays(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 
                           4:list<CellAsArray> cells) throws (1:ClientException e),
  
  /**
   * Open a shared periodic mutator which causes cells to be written asyncronously. 
   * Users beware: calling this method merely writes
   * cells to a local buffer and does not guarantee that the cells have been persisted.
   * If you want guaranteed durability, use the open_mutator+set_cells* interface instead.
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @param mutate_spec - mutator specification
   *
   * @param cell - cell to be written 
   */
  void shared_mutator_set_cell(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 4:Cell cell) 
      throws (1:ClientException e),
  void offer_cell(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 4:Cell cell) 
      throws (1:ClientException e),
  
  /**
   * Alternative to offer_cell interface using array as cell
   */
  void shared_mutator_set_cell_as_array(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 
      4:CellAsArray cell) throws (1:ClientException e),
  void offer_cell_as_array(1:Namespace ns, 2:string table_name, 3:MutateSpec mutate_spec, 
      4:CellAsArray cell) throws (1:ClientException e),

  /**
   * Open a table mutator
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @param flags - mutator flags
   *
   * @param flush_interval - auto-flush interval in milliseconds; 0 disables it.
   *
   * @return mutator id
   */
  Mutator mutator_open(1:Namespace ns, 2:string table_name, 3:i32 flags = 0; 
      4:i32 flush_interval = 0) throws (1:ClientException e),
  Mutator open_mutator(1:Namespace ns, 2:string table_name, 3:i32 flags = 0; 
      4:i32 flush_interval = 0) throws (1:ClientException e),
  
  /**
   * Open an asynchronous table mutator
   *
   * @param ns - namespace id 
   * @param table_name - table name
   * @param future - callback object
   * @param flags - mutator flags
   *
   * @return mutator id
   */
  MutatorAsync async_mutator_open(1:Namespace ns, 2:string table_name, 3:Future future, 
      4:i32 flags = 0) throws (1:ClientException e),
  MutatorAsync open_mutator_async(1:Namespace ns, 2:string table_name, 3:Future future, 
      4:i32 flags = 0) throws (1:ClientException e),

  /**
   * Close a table mutator
   *
   * @param mutator - mutator id to close
   */
  void mutator_close(1:Mutator mutator)
      throws (1:ClientException e),
  void close_mutator(1:Mutator mutator)
      throws (1:ClientException e),
  
  /**
   * Cancel an asynchronous table mutator
   *
   * @param mutator -  mutator id to cancel 
   */
  void async_mutator_cancel(1:MutatorAsync mutator) 
      throws (1:ClientException e),
  void cancel_mutator_async(1:MutatorAsync mutator) 
      throws (1:ClientException e),

  /**
   * Close an asynchronous table mutator
   *
   * @param mutator - mutator id to close
   */
  void async_mutator_close(1:MutatorAsync mutator)
      throws (1:ClientException e),
  void close_mutator_async(1:MutatorAsync mutator)
      throws (1:ClientException e),


  /**
   * Set a cell via mutator
   *
   * @param mutator - mutator id
   *
   * @param cell - the cell to set
   */
  void mutator_set_cell(1:Mutator mutator, 2:Cell cell) throws (1:ClientException e),

  /**
   * Set a cell in the table
   *
   * @param ns - namespace id
   *
   * @param table_name - table name
   *
   * @param cell - the cell to set
   */
  void set_cell(1:Namespace ns, 2:string table_name, 3:Cell cell) throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  void mutator_set_cell_as_array(1:Mutator mutator, 2:CellAsArray cell)
      throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  void set_cell_as_array(1:Namespace ns, 2:string table_name, 3:CellAsArray cell)
      throws (1:ClientException e),

  /**
   * Put a list of cells into a table via mutator
   *
   * @param mutator - mutator id
   *
   * @param cells - a list of cells (a cell with no row key set is assumed
   *        to have the same row key as the previous cell)
   */
  void mutator_set_cells(1:Mutator mutator, 2:list<Cell> cells)
      throws (1:ClientException e),

  /**
   * Put a list of cells into a table
   *
   * @param ns - namespace id
   *
   * @param table_name - table name
   *
   * @param cells - a list of cells (a cell with no row key set is assumed
   *        to have the same row key as the previous cell)
   */
  void set_cells(1:Namespace ns, 2:string table_name, 3:list<Cell> cells)
      throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  void mutator_set_cells_as_arrays(1:Mutator mutator, 2:list<CellAsArray> cells)
      throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  void set_cells_as_arrays(1:Namespace ns, 2:string table_name, 3:list<CellAsArray> cells)
      throws (1:ClientException e),

  /**
   * Alternative interface using buffer of serialized cells
   */
  void mutator_set_cells_serialized(1:Mutator mutator, 2:CellsSerialized cells, 3:bool flush = 0)
      throws (1:ClientException e),

  /**
   * Alternative interface using buffer of serialized cells
   */
   void set_cells_serialized(1:Namespace ns, 2:string table_name, 3:CellsSerialized cells)
      throws (1:ClientException e),

  /**
   * Flush mutator buffers
   */
  void mutator_flush(1:Mutator mutator) throws (1:ClientException e),
  void flush_mutator(1:Mutator mutator) throws (1:ClientException e),
  
  /**
   * Set a cell in the table using an asynchonous mutator
   *
   * @param mutator - mutator id
   *
   * @param cell - the cell to set
   */
  void async_mutator_set_cell(1:MutatorAsync mutator, 2:Cell cell) throws (1:ClientException e),
  void set_cell_async(1:MutatorAsync mutator, 2:Cell cell) throws (1:ClientException e),

  /**
   * Alternative interface using array as cell using an asynchonous mutator 
   */
  void async_mutator_set_cell_as_array(1:MutatorAsync mutator, 2:CellAsArray cell)
      throws (1:ClientException e),
  void set_cell_as_array_async(1:MutatorAsync mutator, 2:CellAsArray cell)
      throws (1:ClientException e),

  /**
   * Put a list of cells into a table using asynchonous mutator 
   *
   * @param mutator - mutator id
   *
   * @param cells - a list of cells (a cell with no row key set is assumed
   *        to have the same row key as the previous cell)
   */
  void async_mutator_set_cells(1:MutatorAsync mutator, 2:list<Cell> cells)
      throws (1:ClientException e),
  void set_cells_async(1:MutatorAsync mutator, 2:list<Cell> cells)
      throws (1:ClientException e),

  /**
   * Alternative interface using array as cell
   */
  void async_mutator_set_cells_as_arrays(1:MutatorAsync mutator, 2:list<CellAsArray> cells)
      throws (1:ClientException e),
  void set_cells_as_arrays_async(1:MutatorAsync mutator, 2:list<CellAsArray> cells)
      throws (1:ClientException e),

  /**
   * Alternative interface using buffer of serialized cells
   */
  void async_mutator_set_cells_serialized(1:MutatorAsync mutator, 2:CellsSerialized cells, 3:bool flush = 0)
      throws (1:ClientException e),
  void set_cells_serialized_async(1:MutatorAsync mutator, 2:CellsSerialized cells, 3:bool flush = 0)
      throws (1:ClientException e),

  /**
   * Flush mutator buffers
   */
  void async_mutator_flush(1:MutatorAsync mutator) throws (1:ClientException e),
  void flush_mutator_async(1:MutatorAsync mutator) throws (1:ClientException e),
  
  /**
   * Check if the namespace exists 
   *
   * @param ns - namespace name
   *
   * @return true if ns exists, false ow
   */
  bool namespace_exists(1:string ns) throws (1:ClientException e),
  bool exists_namespace(1:string ns) throws (1:ClientException e),
 
  /**
   * Check if the table exists 
   *
   * @param ns - namespace id 
   *
   * @param name - table name
   *
   * @return true if table exists, false ow
   */
  bool exists_table(1:Namespace ns, 2:string name) throws (1:ClientException e),
  bool table_exists(1:Namespace ns, 2:string name) throws (1:ClientException e),

  /**
   * Get the id of a table
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @return table id string
   */
  string table_get_id(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  string get_table_id(1:Namespace ns, 2:string table_name) throws (1:ClientException e),

  /**
   * Get the schema of a table as a string (that can be used with create_table)
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @return schema string (in xml)
   */
  string table_get_schema_str(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  string get_schema_str(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  
  /**
   * Get the schema of a table as a string along with column family ids 
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @return schema string (in xml)
   */
  string table_get_schema_str_with_ids(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  string get_schema_str_with_ids(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  
  /**
   * Get the schema of a table as a string (that can be used with create_table)
   *   
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @return schema object describing a table 
   */
  Schema table_get_schema(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  Schema get_schema(1:Namespace ns, 2:string table_name) throws (1:ClientException e),

  /**
   * Get a list of table names in the namespace 
   *
   * @param ns - namespace id 
   *
   * @return a list of table names
   */
  list<string> get_tables(1:Namespace ns) throws (1:ClientException e),
  
  /**
   * Get a list of namespaces and table names table names in the namespace 
   *
   * @param ns - namespace 
   *
   * @return a list of table names
   */
  list<NamespaceListing> namespace_get_listing(1:Namespace ns) throws (1:ClientException e),
  list<NamespaceListing> get_listing(1:Namespace ns) throws (1:ClientException e),
  
  /**
   * Get a list of table splits
   *
   * @param ns - namespace id 
   *
   * @param table_name - table name
   *
   * @return a list of table names
   */
  list<TableSplit> table_get_splits(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  list<TableSplit> get_table_splits(1:Namespace ns, 2:string table_name) throws (1:ClientException e),
  
  /**
   * Drop a namespace 
   *
   * @param ns - namespace name 
   *
   * @param if_exists - if true, don't barf if the table doesn't exist
   */
  void namespace_drop(1:string ns, 2:bool if_exists = 1)
      throws (1:ClientException e),
  void drop_namespace(1:string ns, 2:bool if_exists = 1)
      throws (1:ClientException e),
  
  /**
   * Rename a table
   *
   * @param ns - namespace id
   *
   * @param name - current table name
   *
   * @param new_name - new table name 
   */
  void rename_table(1:Namespace ns, 2:string name, 3:string new_name)
      throws (1:ClientException e),
  void table_rename(1:Namespace ns, 2:string name, 3:string new_name)
      throws (1:ClientException e),

  /**
   * Drop a table
   *
   * @param ns - namespace id
   *
   * @param name - table name
   *
   * @param if_exists - if true, don't barf if the table doesn't exist
   */
  void drop_table(1:Namespace ns, 2:string name, 3:bool if_exists = 1)
      throws (1:ClientException e),
  void table_drop(1:Namespace ns, 2:string name, 3:bool if_exists = 1)
      throws (1:ClientException e),

  /**
   * Generate a GUID
   *
   * GUIDs are globally unique. The generated string is 36 bytes long and 
   * has a format similar to "9cf7da31-307a-4bef-b65e-19fb05aa57d8".
   */
  string generate_guid();

  /**
   * Inserts a unique value into a table
   *
   * This function inserts a unique value into a table. The table must be
   * created with TIME_ORDER DESC, MAX_VERSIONS 1 (although the latter is 
   * optional). 
   *
   * If the value is empty then a new GUID will be assigned 
   * (using @a generate_guid).
   *
   * @param ns - namespace id
   * @param table_name - table name
   * @param key - the Key of the value
   * @param value - the unique value. Can be empty; in this case a new 
   *    guid will be generated internally
   *
   * @return the inserted unique value (which is identical to the @a value 
   *    parameter or a new guid, if value is empty)
   */
  string create_cell_unique(1:Namespace ns, 2:string table_name, 3:Key key,
      4:string value) throws (1:ClientException e),

  /**
   * Retrieves a descriptive error string from an error code
   *
   * @param error_code - the numeric error code
   *
   * @return the descriptive string, or "ERROR NOT REGISTERED" if the error
   *    code is unknown
   */
  string error_get_text(1:i32 error_code),

  /** Status check.
   * @return Status information
   */
  Status status() throws (1:ClientException e),

  /** Shutdown broker. */         
  void shutdown() throws (1:ClientException e);

}
