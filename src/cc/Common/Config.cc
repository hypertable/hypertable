/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
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
#include "Common/Version.h"
#include "Common/Logger.h"
#include "Common/String.h"
#include "Common/Path.h"
#include "Common/FileUtils.h"
#include "Common/Config.h"
#include "Common/SystemInfo.h"

#include <iostream>
#include <fstream>
#include <errno.h>


namespace Hypertable { namespace Config {

// singletons
RecMutex rec_mutex;
PropertiesPtr properties;
String filename;
bool file_loaded = false;
bool allow_unregistered = false;

Desc *cmdline_descp = NULL;
Desc *cmdline_hidden_descp = NULL;
PositionalDesc *cmdline_positional_descp = NULL;
Desc *file_descp = NULL;

int line_length() {
  int n = System::term_info().num_cols;
  return n > 0 ? n : 80;
}

String usage_str(const char *usage) {
  if (!usage)
    usage = "Usage: %s [options]\n\nOptions";

  if (strstr(usage, "%s"))
    return format(usage, System::exe_name.c_str());

  return usage;
}

Desc &cmdline_desc(const char *usage) {
  ScopedRecLock lock(rec_mutex);

  if (!cmdline_descp)
    cmdline_descp = new Desc(usage_str(usage), line_length());

  return *cmdline_descp;
}

Desc &cmdline_hidden_desc() {
  ScopedRecLock lock(rec_mutex);

  if (!cmdline_hidden_descp)
    cmdline_hidden_descp = new Desc();

  return *cmdline_hidden_descp;
}

PositionalDesc &cmdline_positional_desc() {
  ScopedRecLock lock(rec_mutex);

  if (!cmdline_positional_descp)
    cmdline_positional_descp = new PositionalDesc();

  return *cmdline_positional_descp;
}

void cmdline_desc(const Desc &desc) {
  ScopedRecLock lock(rec_mutex);

  if (cmdline_descp)
    delete cmdline_descp;

  cmdline_descp = new Desc(desc);
}

Desc &file_desc(const char *usage) {
  ScopedRecLock lock(rec_mutex);

  if (!file_descp)
    file_descp = new Desc(usage ? usage : "Config Properties", line_length());

  return *file_descp;
}

void file_desc(const Desc &desc) {
  ScopedRecLock lock(rec_mutex);

  if (file_descp)
    delete file_descp;

  file_descp = new Desc(desc);
}

void DefaultPolicy::init_options() {
  String default_config;
  HT_EXPECT(!System::install_dir.empty(), Error::FAILED_EXPECTATION);

  // Detect installed path and assume the layout, otherwise assume the
  // default config file in the current directory.
  if (System::install_dir == boost::filesystem::current_path().string()) {
    Path exe(System::proc_info().exe);
    default_config = exe.parent_path() == System::install_dir
        ? "hypertable.cfg" : "conf/hypertable.cfg";
  }
  else {
    Path config(System::install_dir);
    config /= "conf/hypertable.cfg";
    default_config = config.string();
  }
  String default_data_dir = System::install_dir;

  cmdline_desc().add_options()
    ("help,h", "Show this help message and exit")
    ("help-config", "Show help message for config properties")
    ("version", "Show version information and exit")
    ("verbose,v", boo()->zero_tokens()->default_value(false),
        "Show more verbose output")
    ("debug", boo()->zero_tokens()->default_value(false),
        "Show debug output (shortcut of --logging-level debug)")
    ("quiet", boo()->zero_tokens()->default_value(false), "Negate verbose")
    ("silent", boo()->zero_tokens()->default_value(false),
        "Show as little output as possible")
    ("logging-level,l", str()->default_value("info"),
        "Logging level: debug, info, notice, warn, error, crit, alert, fatal")
    ("config", str()->default_value(default_config), "Configuration file.\n")
    ("induce-failure", str(), "Arguments for inducing failure")
    ;
  alias("logging-level", "Hypertable.Logging.Level");
  alias("verbose", "Hypertable.Verbose");
  alias("silent", "Hypertable.Silent");

  // pre boost 1.35 doesn't support allow_unregistered, so we have to have the
  // full cfg definition here, which might not be a bad thing.
  file_desc().add_options()
    ("Comm.DispatchDelay", i32()->default_value(0), "[TESTING ONLY] "
        "Delay dispatching of read requests by this number of milliseconds")
    ("Comm.UsePoll", boo()->default_value(false), "Use poll() interface")
    ("Hypertable.Verbose", boo()->default_value(false),
        "Enable verbose output (system wide)")
    ("Hypertable.Silent", boo()->default_value(false),
        "Disable verbose output (system wide)")
    ("Hypertable.Logging.Level", str()->default_value("info"),
        "Set system wide logging level (default: info)")
    ("Hypertable.DataDirectory", str()->default_value(default_data_dir),
        "Hypertable data directory root")
    ("Hypertable.Client.Workers", i32()->default_value(20),
        "Number of client worker threads created")
    ("Hypertable.Connection.Retry.Interval", i32()->default_value(10000),
        "Average time, in milliseconds, between connection retry atempts")
    ("Hypertable.LoadMetrics.Interval", i32()->default_value(3600), "Period of "
        "time, in seconds, between writing metrics to sys/RS_METRICS")
    ("Hypertable.Request.Timeout", i32()->default_value(600000), "Length of "
        "time, in milliseconds, before timing out requests (system wide)")
    ("Hypertable.MetaLog.SkipErrors", boo()->default_value(false), "Skipping "
        "errors instead of throwing exceptions on metalog errors")
    ("Hypertable.Network.Interface", str(),
     "Use this interface for network communication")
    ("CephBroker.Port", i16()->default_value(38030),
     "Port number on which to listen (read by CephBroker only)")
    ("CephBroker.Workers", i32()->default_value(20),
     "Number of Ceph broker worker threads created, maybe")
    ("CephBroker.MonAddr", str(),
     "Ceph monitor address to connect to")
    ("CephBroker.Id", str()->default_value("cb1"),
     "Id for this Ceph broker")
    ("CephBroker.RootDir", str()->default_value("/"),
     "Root directory for this Ceph broker")
    ("HdfsBroker.Port", i16(),
        "Port number on which to listen (read by HdfsBroker only)")
    ("HdfsBroker.fs.default.name", str(), "Hadoop Filesystem "
        "default name, same as fs.default.name property in Hadoop config "
        "(e.g. hdfs://localhost:9000)")
    ("DfsBroker.Hdfs.NameNode.Host", str()->default_value("default"),
        "Name of host on which HDFS NameNode is running")
    ("DfsBroker.Hdfs.NameNode.Port", i16()->default_value(0),
        "Port number on which HDFS NameNode is running")
    ("HdfsBroker.Workers", i32(),
        "Number of HDFS broker worker threads created")
    ("HdfsBroker.Reactors", i32(),
        "Number of HDFS broker communication reactor threads created")
    ("Kfs.Broker.Workers", i32()->default_value(20), "Number of worker "
        "threads for Kfs broker")
    ("Kfs.Broker.Reactors", i32(), "Number of Kfs broker reactor threads")
    ("Kfs.MetaServer.Name", str(), "Hostname of Kosmos meta server")
    ("Kfs.MetaServer.Port", i16(), "Port number for Kosmos meta server")
    ("DfsBroker.Local.DirectIO", boo()->default_value(false),
        "Read and write files using direct i/o")
    ("DfsBroker.Local.Port", i16()->default_value(38030),
        "Port number on which to listen (read by LocalBroker only)")
    ("DfsBroker.Local.Root", str(), "Root of file and directory "
        "hierarchy for local broker (if relative path, then is relative to "
        "the Hypertable data directory root)")
    ("DfsBroker.Local.Workers", i32()->default_value(20),
        "Number of local broker worker threads created")
    ("DfsBroker.Local.Reactors", i32(),
        "Number of local broker communication reactor threads created")
    ("DfsBroker.Host", str()->default_value("localhost"),
        "Host on which the DFS broker is running (read by clients only)")
    ("DfsBroker.Port", i16()->default_value(38030),
        "Port number on which DFS broker is listening (read by clients only)")
    ("DfsBroker.Timeout", i32(), "Length of time, "
        "in milliseconds, to wait before timing out DFS Broker requests. This "
        "takes precedence over Hypertable.Request.Timeout")
    ("Hyperspace.Timeout", i32()->default_value(30000), "Timeout (millisec) "
        "for hyperspace requests (preferred to Hypertable.Request.Timeout")
    ("Hyperspace.Maintenance.Interval", i32()->default_value(60000), "Hyperspace "
        " maintenance interval (checkpoint BerkeleyDB, log cleanup etc)")
    ("Hyperspace.Checkpoint.Size", i32()->default_value(1*M), "Run BerkeleyDB checkpoint"
        " when logs exceed this size limit")
    ("Hyperspace.Client.Datagram.SendPort", i16()->default_value(0),
        "Client UDP send port for keepalive packets")
    ("Hyperspace.LogGc.Interval", i32()->default_value(60000), "Check for unused BerkeleyDB "
        "log files after this much time")
    ("Hyperspace.LogGc.MaxUnusedLogs", i32()->default_value(200), "Number of unused BerkeleyDB "
        " to keep around in case of lagging replicas")
    ("Hyperspace.Replica.Host", strs(), "Hostname of Hyperspace replica")
    ("Hyperspace.Replica.Port", i16()->default_value(38040),
        "Port number on which Hyperspace is or should be listening for requests")
    ("Hyperspace.Replica.Replication.Port", i16()->default_value(38041),
        "Hyperspace replication port ")
    ("Hyperspace.Replica.Replication.Timeout", i32()->default_value(10000),
        "Hyperspace replication master dies if it doesn't receive replication acknowledgement "
        "within this period")
    ("Hyperspace.Replica.Workers", i32()->default_value(20),
        "Number of Hyperspace Replica worker threads created")
    ("Hyperspace.Replica.Reactors", i32(),
        "Number of Hyperspace Master communication reactor threads created")
    ("Hyperspace.Replica.Dir", str(), "Root of hyperspace file and directory "
        "heirarchy in local filesystem (if relative path, then is relative to "
        "the Hypertable data directory root)")
    ("Hyperspace.KeepAlive.Interval", i32()->default_value(10000),
        "Hyperspace Keepalive interval (see Chubby paper)")
    ("Hyperspace.Lease.Interval", i32()->default_value(60000),
        "Hyperspace Lease interval (see Chubby paper)")
    ("Hyperspace.GracePeriod", i32()->default_value(60000),
        "Hyperspace Grace period (see Chubby paper)")
    ("Hyperspace.Session.Reconnect", boo()->default_value(false),
        "Reconnect to Hyperspace on session expiry")
    ("Hypertable.Directory", str()->default_value("hypertable"),
        "Top-level hypertable directory name")
    ("Hypertable.Monitoring.Interval", i32()->default_value(30000),
        "Monitoring statistics gathering interval (in milliseconds)")
    ("Hypertable.LoadBalancer.Enable", boo()->default_value(true),
        "Enable automatic load balancing")
    ("Hypertable.LoadBalancer.Interval", i32()->default_value(86400000),
        "Time interval between LoadBalancer operations")
    ("Hypertable.LoadBalancer.WindowStart", str()->default_value("00:00:01"),
        "Time of day at which LoadBalancer balance window starts")
    ("Hypertable.LoadBalancer.WindowEnd", str()->default_value("00:03:00"),
        "Time of day at which the LoadBalancer balance window ends")
    ("Hypertable.LoadBalancer.ServerWaitInterval", i32()->default_value(300000),
        "Amount of time to wait before running balancer when a new RangeServer is detected")
    ("Hypertable.LoadBalancer.LoadavgThreshold", f64()->default_value(0.25),
        "Servers with loadavg above this much above the mean will be considered by the "
        "load balancer to be overloaded")
    ("Hypertable.HqlInterpreter.Mutator.NoLogSync", boo()->default_value(false),
        "Suspends CommitLog sync operation on updates until command completion")
    ("Hypertable.Mutator.FlushDelay", i32()->default_value(0), "Number of "
        "milliseconds to wait prior to flushing scatter buffers (for testing)")
    ("Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer",
     i32()->default_value(10*M), "Amount of updates (bytes) accumulated for a "
        "single server to trigger a scatter buffer flush")
    ("Hypertable.Mutator.ScatterBuffer.FlushLimit.Aggregate",
     i64()->default_value(50*M), "Amount of updates (bytes) accumulated for "
        "all servers to trigger a scatter buffer flush")
    ("Hypertable.Scanner.QueueSize",
     i32()->default_value(5), "Size of Scanner ScanBlock queue")
    ("Hypertable.LocationCache.MaxEntries", i64()->default_value(1*M),
        "Size of range location cache in number of entries")
    ("Hypertable.Master.Host", str(),
        "Host on which Hypertable Master is running")
    ("Hypertable.Master.Port", i16()->default_value(38050),
        "Port number on which Hypertable Master is or should be listening")
    ("Hypertable.Master.Workers", i32()->default_value(100),
        "Number of Hypertable Master worker threads created")
    ("Hypertable.Master.Reactors", i32(),
        "Number of Hypertable Master communication reactor threads created")
    ("Hypertable.Master.Gc.Interval", i32()->default_value(300000),
        "Garbage collection interval in milliseconds by Master")
    ("Hypertable.Master.Locations.IncludeMasterHash", boo()->default_value(false),
        "Includes master hash (host:port) in RangeServer location id")
    ("Hypertable.Master.Split.SoftLimitEnabled", boo()->default_value(true),
        "Enable aggressive splitting of tables with little data to spread out ranges")
    ("Hypertable.RangeServer.AccessGroup.GarbageThreshold.Percentage",
     i32()->default_value(20), "Perform major compaction when garbage accounts "
     "for this percentage of the data")
    ("Hypertable.RangeServer.MemoryLimit", i64(), "RangeServer memory limit")
    ("Hypertable.RangeServer.MemoryLimit.Percentage", i32()->default_value(60),
     "RangeServer memory limit specified as percentage of physical RAM")
    ("Hypertable.RangeServer.LowMemoryLimit.Percentage", i32()->default_value(10),
     "Amount of memory to free in low memory condition as percentage of RangeServer memory limit")
    ("Hypertable.RangeServer.MemoryLimit.EnsureUnused", i64(), "Amount of unused physical memory")
    ("Hypertable.RangeServer.MemoryLimit.EnsureUnused.Percentage", i32(),
     "Amount of unused physical memory specified as percentage of physical RAM")
    ("Hypertable.RangeServer.Port", i16()->default_value(38060),
        "Port number on which range servers are or should be listening")
    ("Hypertable.RangeServer.AccessGroup.CellCache.PageSize",
     i32()->default_value(512*KiB), "Page size for CellCache pool allocator")
    ("Hypertable.RangeServer.AccessGroup.CellCache.ScannerCacheSize",
     i32()->default_value(1024), "CellCache scanner cache size")
    ("Hypertable.RangeServer.AccessGroup.ShadowCache",
     boo()->default_value(false), "Enable CellStore shadow caching")
    ("Hypertable.RangeServer.AccessGroup.MaxMemory", i64()->default_value(1*G),
        "Maximum bytes consumed by an Access Group")
    ("Hypertable.RangeServer.CellStore.TargetSize.Minimum",
        i64()->default_value(10*MiB), "Target minimum size for CellStores")
    ("Hypertable.RangeServer.CellStore.TargetSize.Window",
        i64()->default_value(30*MiB), "Size window above target minimum for "
        "CellStores in which merges will be considered")
    ("Hypertable.RangeServer.CellStore.Merge.RunLengthThreshold", i32()->default_value(10),
        "Trigger a merge if an adjacent run of merge candidate CellStores exceeds this length")
    ("Hypertable.RangeServer.CellStore.DefaultBlockSize",
        i32()->default_value(64*KiB), "Default block size for cell stores")
    ("Hypertable.RangeServer.Data.DefaultReplication",
        i32()->default_value(-1), "Default replication for data")
    ("Hypertable.RangeServer.CellStore.DefaultCompressor",
        str()->default_value("snappy"), "Default compressor for cell stores")
    ("Hypertable.RangeServer.CellStore.DefaultBloomFilter",
        str()->default_value("rows"), "Default bloom filter for cell stores")
    ("Hypertable.RangeServer.CellStore.SkipNotFound",
        boo()->default_value(false), "Skip over cell stores that are non-existent")
    ("Hypertable.RangeServer.IgnoreClockSkewErrors",
        boo()->default_value(false), "Ignore clock skew errors")
    ("Hypertable.RangeServer.CommitInterval", i32()->default_value(50),
     "Default minimum group commit interval in milliseconds")
    ("Hypertable.RangeServer.BlockCache.Compressed", boo()->default_value(true),
        "Controls whether or not block cache stores compressed blocks")
    ("Hypertable.RangeServer.BlockCache.MinMemory", i64()->default_value(0),
        "Minimum size of block cache")
    ("Hypertable.RangeServer.BlockCache.MaxMemory", i64()->default_value(-1),
        "Maximum (target) size of block cache")
    ("Hypertable.RangeServer.QueryCache.MaxMemory", i64()->default_value(50*M),
        "Maximum size of query cache")
    ("Hypertable.RangeServer.Range.SplitSize", i64()->default_value(256*MiB),
        "Size of range in bytes before splitting")
    ("Hypertable.RangeServer.Range.MaximumSize", i64()->default_value(3*G),
        "Maximum size of a range in bytes before updates get throttled")
    ("Hypertable.RangeServer.Range.MetadataSplitSize", i64(), "Size of METADATA "
        "range in bytes before splitting (for testing)")
    ("Hypertable.RangeServer.Range.SplitOff", str()->default_value("high"),
        "Portion of range to split off (high or low)")
    ("Hypertable.RangeServer.ClockSkew.Max", i32()->default_value(3*M),
        "Maximum amount of clock skew (microseconds) the system will tolerate")
    ("Hypertable.RangeServer.CommitLog.DfsBroker.Host", str(),
        "Host of DFS Broker to use for Commit Log")
    ("Hypertable.RangeServer.CommitLog.DfsBroker.Port", i16(),
        "Port of DFS Broker to use for Commit Log")
    ("Hypertable.RangeServer.CommitLog.PruneThreshold.Min", i64()->default_value(1*G),
        "Lower threshold for amount of outstanding commit log before pruning")
    ("Hypertable.RangeServer.CommitLog.PruneThreshold.Max", i64(),
        "Upper threshold for amount of outstanding commit log before pruning")
    ("Hypertable.RangeServer.CommitLog.PruneThreshold.Max.MemoryPercentage",
        i32()->default_value(50), "Upper threshold in terms of % RAM for "
        "amount of outstanding commit log before pruning")
    ("Hypertable.RangeServer.CommitLog.RollLimit", i64()->default_value(100*M),
        "Roll commit log after this many bytes")
    ("Hypertable.RangeServer.CommitLog.Compressor",
        str()->default_value("quicklz"),
       "Commit log compressor to use (zlib, lzo, quicklz, snappy, bmz, none)")
    ("Hypertable.RangeServer.Testing.MaintenanceNeeded.PauseInterval", i32()->default_value(0),
        "TESTING:  After update, if range needs maintenance, pause for this number of milliseconds")
    ("Hypertable.RangeServer.UpdateCoalesceLimit", i64()->default_value(5*M),
        "Amount of update data to coalesce into single commit log sync")
    ("Hypertable.Metadata.Replication", i32()->default_value(-1),
        "Replication factor for commit log files")
    ("Hypertable.CommitLog.RollLimit", i64()->default_value(100*M),
        "Roll commit log after this many bytes")
    ("Hypertable.CommitLog.Compressor", str()->default_value("quicklz"),
        "Commit log compressor to use (zlib, lzo, quicklz, snappy, bmz, none)")
    ("Hypertable.CommitLog.SkipErrors", boo()->default_value(false),
        "Skip over any corruption encountered in the commit log")
    ("Hypertable.RangeServer.Scanner.Ttl", i32()->default_value(1800*K),
        "Number of milliseconds of inactivity before destroying scanners")
    ("Hypertable.RangeServer.Scanner.BufferSize", i64()->default_value(1*M),
        "Size of transfer buffer for scan results")
    ("Hypertable.RangeServer.Timer.Interval", i32()->default_value(20000),
        "Timer interval in milliseconds (reaping scanners, purging commit logs, etc.)")
    ("Hypertable.RangeServer.Maintenance.Interval", i32()->default_value(30000),
        "Maintenance scheduling interval in milliseconds")
    ("Hypertable.RangeServer.Maintenance.MergesPerInterval", i32(),
        "Limit on number of merging tasks to create per maintenance interval")
    ("Hypertable.RangeServer.Maintenance.MergingCompaction.Delay", i32()->default_value(900000),
        "Millisecond delay before scheduling merging compactions in non-low memory mode")
    ("Hypertable.RangeServer.Maintenance.MoveCompactionsPerInterval", i32()->default_value(2),
        "Limit on number of major compactions due to move per maintenance interval")
    ("Hypertable.RangeServer.Monitoring.DataDirectories", str()->default_value("/"),
        "Comma-separated list of directory mount points of disk volumes to monitor")
    ("Hypertable.RangeServer.Workers", i32()->default_value(50),
        "Number of Range Server worker threads created")
    ("Hypertable.RangeServer.Reactors", i32(),
        "Number of Range Server communication reactor threads created")
    ("Hypertable.RangeServer.MaintenanceThreads", i32(),
        "Number of maintenance threads.  Default is min(2, number-of-cores).")
    ("Hypertable.RangeServer.UpdateDelay", i32()->default_value(0),
        "Number of milliseconds to wait before carrying out an update (TESTING)")
    ("Hypertable.RangeServer.ProxyName", str()->default_value(""),
        "Use this value for the proxy name (if set) instead of reading from run dir.")
    ("ThriftBroker.Timeout", i32(), "Timeout (ms) for thrift broker")
    ("ThriftBroker.Port", i16()->default_value(38080), "Port number for "
        "thrift broker")
    ("ThriftBroker.Future.Capacity", i32()->default_value(50*M), "Capacity "
        "of result queue (in bytes) for Future objects")
    ("ThriftBroker.NextThreshold", i32()->default_value(128*K), "Total size  "
        "threshold for (size of cell data) for thrift broker next calls")
    ("ThriftBroker.API.Logging", boo()->default_value(false), "Enable or "
        "disable Thrift API logging")
    ("ThriftBroker.Mutator.FlushInterval", i32()->default_value(1000),
        "Maximum flush interval in milliseconds")
    ("ThriftBroker.Workers", i32()->default_value(50), "Number of "
        "worker threads for thrift broker")
    ("ThriftBroker.Hyperspace.Session.Reconnect", boo()->default_value(true),
        "ThriftBroker will reconnect to Hyperspace on session expiry")
    ;
  alias("Hypertable.RangeServer.CommitLog.RollLimit",
        "Hypertable.CommitLog.RollLimit");
  alias("Hypertable.RangeServer.CommitLog.Compressor",
        "Hypertable.CommitLog.Compressor");
  // add config file desc to cmdline hidden desc, so people can override
  // any config values on the command line
  cmdline_hidden_desc().add(file_desc());
}

void parse_args(int argc, char *argv[]) {
  ScopedRecLock lock(rec_mutex);

  HT_TRY("parsing init arguments",
    properties->parse_args(argc, argv, cmdline_desc(), cmdline_hidden_descp,
                           cmdline_positional_descp, allow_unregistered));
  // some built-in behavior
  if (has("help")) {
    std::cout << cmdline_desc() << std::flush;
    _exit(0);
  }

  if (has("help-config")) {
    std::cout << file_desc() << std::flush;
    _exit(0);
  }

  if (has("version")) {
    std::cout << version_string() << std::endl;
    _exit(0);
  }

  filename = get_str("config");

  // Only try to parse config file if it exists or not default
  if (FileUtils::exists(filename)) {
    parse_file(filename, cmdline_hidden_desc());
    file_loaded = true;
  }
  else if (!defaulted("config"))
    HT_THROW(Error::FILE_NOT_FOUND, filename);

  sync_aliases();       // call before use
}

void
parse_file(const String &fname, const Desc &desc) {
  properties->load(fname, desc, allow_unregistered);
}

void alias(const String &cmdline_opt, const String &file_opt, bool overwrite) {
  properties->alias(cmdline_opt, file_opt, overwrite);
}

void sync_aliases() {
  properties->sync_aliases();
}

void DefaultPolicy::init() {
  String loglevel = get_str("logging-level");
  bool verbose = get_bool("verbose");

  if (verbose && get_bool("quiet")) {
    verbose = false;
    properties->set("verbose", false);
  }
  if (get_bool("debug")) {
    loglevel = "debug";
    properties->set("logging-level", loglevel);
  }

  if (loglevel == "info")
    Logger::set_level(Logger::Priority::INFO);
  else if (loglevel == "debug")
    Logger::set_level(Logger::Priority::DEBUG);
  else if (loglevel == "notice")
    Logger::set_level(Logger::Priority::NOTICE);
  else if (loglevel == "warn")
    Logger::set_level(Logger::Priority::WARN);
  else if (loglevel == "error")
    Logger::set_level(Logger::Priority::ERROR);
  else if (loglevel == "crit")
    Logger::set_level(Logger::Priority::CRIT);
  else if (loglevel == "alert")
    Logger::set_level(Logger::Priority::ALERT);
  else if (loglevel == "fatal")
    Logger::set_level(Logger::Priority::FATAL);
  else {
    HT_ERROR_OUT << "unknown logging level: "<< loglevel << HT_END;
    _exit(0);
  }
  if (verbose) {
    HT_NOTICE_OUT <<"Initializing "<< System::exe_name <<" (Hypertable "<< version_string()
                  <<")..." << HT_END;
  }
}

bool allow_unregistered_options(bool choice) {
  ScopedRecLock lock(rec_mutex);
  bool old = allow_unregistered;
  allow_unregistered = choice;
  return old;
}

bool allow_unregistered_options() {
  ScopedRecLock lock(rec_mutex);
  return allow_unregistered;
}

void cleanup() {
  ScopedRecLock lock(rec_mutex);
  properties = 0;
  if (cmdline_descp) {
    delete cmdline_descp;
    cmdline_descp = 0;
  }
  if (cmdline_hidden_descp) {
    delete cmdline_hidden_descp;
    cmdline_hidden_descp = 0;
  }
  if (cmdline_positional_descp) {
    delete cmdline_positional_descp;
    cmdline_positional_descp = 0;
  }
  if (file_descp) {
    delete file_descp;
    file_descp = 0;
  }
}

}} // namespace Hypertable::Config

