/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
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

/** @file
 * Configuration settings.
 * This file contains the global configuration settings (read from file or
 * from a command line parameter).
 */

#include <Common/Compat.h>
#include <Common/Version.h>
#include <Common/Logger.h>
#include <Common/String.h>
#include <Common/Path.h>
#include <Common/FileUtils.h>
#include <Common/Config.h>
#include <Common/SystemInfo.h>

#include <fstream>
#include <iostream>
#include <mutex>

#include <errno.h>

#include <boost/algorithm/string.hpp>

namespace Hypertable { namespace Config {

// singletons
std::recursive_mutex rec_mutex;
PropertiesPtr properties;

static String filename;
static bool allow_unregistered = false;

static Desc *cmdline_descp = NULL;
static Desc *cmdline_hidden_descp = NULL;
static Desc *file_descp = NULL;

static int terminal_line_length() {
  int n = System::term_info().num_cols;
  return n > 0 ? n : 80;
}

static String usage_str(const char *usage) {
  if (!usage)
    usage = "Usage: %s [options]\n\nOptions";

  if (strstr(usage, "%s"))
    return format(usage, System::exe_name.c_str());

  return usage;
}

Desc &cmdline_desc(const char *usage) {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);

  if (!cmdline_descp)
    cmdline_descp = new Desc(usage_str(usage), terminal_line_length());

  return *cmdline_descp;
}

void cmdline_desc( Desc &desc) {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);

  if (cmdline_descp)
    delete cmdline_descp;

  cmdline_descp = new Desc(desc);
}

Desc &cmdline_hidden_desc() {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);

  if (!cmdline_hidden_descp)
    cmdline_hidden_descp = new Desc();

  return *cmdline_hidden_descp;
}

Desc &file_desc(const char *usage) {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);

  if (!file_descp)
    file_descp = new Desc(usage ? usage : "Config Properties",
            terminal_line_length());

  return *file_descp;
}

void file_desc( Desc &desc) {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);

  if (file_descp)
    delete file_descp;

  file_descp = new Desc(desc);
}

void parse_args(int argc, char *argv[]) {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);

  properties->load_from(
      Config::Parser(argc, argv, cmdline_desc(), cmdline_hidden_desc(), 
                    allow_unregistered_options()).get_options());
    
  // some built-in behavior
  if (has("help")) {
    std::cout << cmdline_desc() << std::flush;
    std::quick_exit(EXIT_SUCCESS);
  }

  if (has("help-config")) {
    std::cout << file_desc() << std::flush;
    std::quick_exit(EXIT_SUCCESS);
  }

  if (has("version")) {
    std::cout << version_string() << std::endl;
    std::quick_exit(EXIT_SUCCESS);
  }

  if (!has("config")) 
    // continue with Policies do not set/require config
    return;

  filename = properties->get_str("config");

  // Only try to parse config file if it exists or not default
  if (FileUtils::exists(filename)) {
    parse_file(filename, cmdline_hidden_desc());
    // Inforce cmdline properties
    properties->load_from(
        Config::Parser(argc, argv, cmdline_desc(), cmdline_hidden_desc(), 
                        allow_unregistered_options()).get_options());
  } else if (!defaulted("config"))
    HT_THROW(Error::FILE_NOT_FOUND, filename);

}

void parse_file(const String &fname, Desc &desc) {
    properties->load(fname, desc, allow_unregistered);
    properties->load_files_by(
        "Hypertable.Config.OnFileChange.file", desc, allow_unregistered);
}

String reparse_file(const String &fname) {
	std::lock_guard<std::recursive_mutex> lock(rec_mutex);
    String filename = fname;
    boost::trim(filename);
    if(filename.empty())
	    filename = properties->get_str("config");
	return properties->reload(filename, cmdline_hidden_desc());
}

void alias(const String &cmdline_opt, const String &file_opt) {
  properties->alias(cmdline_opt, file_opt);
}

bool allow_unregistered_options(bool choice) {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);
  bool old = allow_unregistered;
  allow_unregistered = choice;
  return old;
}

bool allow_unregistered_options() {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);
  return allow_unregistered;
}

bool has(const String &name) {
  HT_ASSERT(properties);
  return properties->has(name);
}

bool defaulted(const String &name) {
  HT_ASSERT(properties);
  return properties->defaulted(name);
}

void cleanup() {
  std::lock_guard<std::recursive_mutex> lock(rec_mutex);
  properties = 0;
  if (cmdline_descp) {
    delete cmdline_descp;
    cmdline_descp = 0;
  }
  if (cmdline_hidden_descp) {
    delete cmdline_hidden_descp;
    cmdline_hidden_descp = 0;
  }
  if (file_descp) {
    delete file_descp;
    file_descp = 0;
  }
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
    
  gEnumExt logging_level(Logger::Priority::INFO);
  logging_level.set_from_string(Logger::cfg::from_string).set_repr(Logger::cfg::repr);
        
  cmdline_desc().add_options()
    ("help,h", "Show this help message and exit")
    ("help-config", "Show help message for config properties")
    ("version", "Show version information and exit")
    ("verbose,v", g_boo(false)->zero_token(), "Show more verbose output")
    ("debug", boo(false)->zero_token(), "Show debug output (shortcut of --logging-level debug)")
    ("quiet", boo(false)->zero_token(), "Negate verbose")
    ("silent", boo()->zero_token(),
     "as Not Interactive or Show as little output as possible")
    ("logging-level,l", g_enum_ext(logging_level), 
     "Logging level: debug, info, notice, warn, error, crit, alert, fatal")
    ("config", str(default_config), "Configuration file.\n")
    ("induce-failure", str(), "Arguments for inducing failure")
    ("timeout,t", i32(), "System wide timeout in milliseconds")
    ;
  alias("logging-level", "Hypertable.Logging.Level");
  alias("verbose", "Hypertable.Verbose");
  alias("silent", "Hypertable.Silent");
  alias("timeout", "Hypertable.Request.Timeout");

  // TODO: 
  //  * avoid of loading all properties
  //  * use of essential configurations file for configurations definitions
  //  * load essential on groups of init requirments eg. Config::load_groups(['pre.']))
  //    use here as list of essential config groups to load 

  // pre boost 1.35 doesn't support allow_unregistered, so we have to have the
  // full cfg definition here, which might not be a bad thing.
  
  file_desc().add_options()
    ("Comm.DispatchDelay", i32(0), "[TESTING ONLY] "
        "Delay dispatching of read requests by this number of milliseconds")
    ("Comm.UsePoll", boo(false), "Use POSIX poll() interface")
    ("Hypertable.Cluster.Name", str(),
     "Name of cluster used in Monitoring UI and admin notification messages")
    ("Hypertable.Verbose", g_boo(false),
        "Enable verbose output (system wide)")
    ("Hypertable.Silent", boo(),
        "Disable verbose output (system wide)")
    ("Hypertable.Logging.Level", g_enum_ext(logging_level),
        "Set system wide logging level (default: info)")
    ("Hypertable.Config.OnFileChange.file", strs(),
        "a Set of Config File/s loaded on start or change")
    ("Hypertable.Config.OnFileChange.Reload", g_boo(false),
        "Set Config File Listener for Reloading cfg on Change")
    ("Hypertable.Config.OnFileChange.Reload.Interval", g_i32(600000),
        "Interval in milliseconds of checking on cfg file")
    ("Hypertable.DataDirectory", str(default_data_dir),
        "Hypertable data directory root")
    ("Hypertable.Client.Workers", i32(20),
        "Number of client worker threads created")
    ("Hypertable.Connection.Retry.Interval", i32(10000),
        "Average time, in milliseconds, between connection retry atempts")
    ("Hypertable.LogFlushMethod.Meta", str("SYNC"),
        "Log flush method for metadata (FLUSH flushes data to replicas, SYNC "
        "persists data all the way down to physical storage)")
    ("Hypertable.LogFlushMethod.User", str("FLUSH"),
        "Log flush method for user data (FLUSH flushes data to replicas, SYNC "
        "persists data all the way down to physical storage)")
    ("Hypertable.Metrics.Ganglia.Disable", boo(false),
        "Disable publishing of metrics to Ganglia")
    ("Hypertable.Metrics.Ganglia.Port", i16(15860),
        "UDP Port on which Hypertable gmond python extension module listens for metrics")
    ("Hypertable.LoadMetrics.Interval", i32(3600), "Period of "
        "time, in seconds, between writing metrics to sys/RS_METRICS")
    ("Hypertable.Request.Timeout", i32(600000), "Length of "
        "time, in milliseconds, before timing out requests (system wide)")
    ("Hypertable.MetaLog.HistorySize", i32(30), "Number "
        "of old MetaLog files to retain for historical purposes")
    ("Hypertable.MetaLog.MaxFileSize", i64(100*M), "Maximum "
        "size a MetaLog file can grow before it is compacted")
    ("Hypertable.MetaLog.SkipErrors", boo(false), "Skipping "
        "errors instead of throwing exceptions on metalog errors")
    ("Hypertable.MetaLog.WriteInterval", i32(30),
        "Minimum write interval for Metalog in milliseconds")
    ("Hypertable.Network.Interface", str(),
     "Use this interface for network communication")
    ("FsBroker.Listen.Port", i16(15863),
        "Port number on which to listen (read by Broker only)")
    ("FsBroker.Listen.Workers", i32(20),
        "Number of broker worker threads created")
    ("FsBroker.Listen.Reactors", i32(),
        "Number of broker communication reactor threads created")
    ("FsBroker.Timeout", i32(), "Length of time, "
        "in milliseconds, to wait before timing out FS Broker requests. This "
        "takes precedence over Hypertable.Request.Timeout")
    ("FsBroker.DisableFileRemoval", boo(false),
        "Rename files with .deleted extension instead of removing (for testing)")
    /*("FsBroker.Hdfs.NameNode.Host", str("default"),
        "Name of host on which HDFS NameNode is running")
    ("FsBroker.Hdfs.NameNode.Port", i16(0),
        "Port number on which HDFS NameNode is running")*/
    ("FsBroker.Hdfs.fs.default.name", str(), "Hadoop Filesystem "
        "default name, same as fs.default.name property in Hadoop config "
        "(e.g. hdfs://localhost:9000)")
    ("FsBroker.Hdfs.Hadoop.ConfDir", str(), "Hadoop configuration directory "
        "(e.g. /etc/hadoop/conf or /usr/lib/hadoop/conf)")
    ("FsBroker.Hdfs.SyncBlock", boo(true),
        "Pass SYNC_BLOCK flag to Filesystem.create() when creating files")
    ("FsBroker.Ceph.MonAddr", str(), "Ceph monitor address to connect to")
    ("FsBroker.Kfs.MetaServer.Name", str(), "Hostname of Kosmos meta server")
    ("FsBroker.Kfs.MetaServer.Port", i16(), "Port number for Kosmos meta server")
    ("FsBroker.Qfs.MetaServer.Name", str("localhost"), "Hostname of QFS meta server")
    ("FsBroker.Qfs.MetaServer.Port", i16(20000), "Port number for QFS meta server")
    ("FsBroker.Local.DirectIO", boo(false), "Read and write files using direct i/o")
    ("FsBroker.Local.Root", str(), "Root of file and directory "
        "hierarchy for local broker (if relative path, then is relative to "
        "the Hypertable data directory root)")
    ("FsBroker.Host", str("localhost"),
        "Host on which the FS broker is running (read by clients only)")
    ("FsBroker.Port", i16(15863),
        "Port number on which FS broker is listening (read by clients only)")
    ("Hyperspace.Timeout", i32(30000), "Timeout (millisec) "
        "for hyperspace requests (preferred to Hypertable.Request.Timeout")
    ("Hyperspace.Maintenance.Interval", g_i32(60000), "Hyperspace "
        " maintenance interval (checkpoint BerkeleyDB, log cleanup etc)")
    ("Hyperspace.Checkpoint.Size", g_i32(1*M), "Run BerkeleyDB checkpoint"
        " when logs exceed this size limit")
    ("Hyperspace.Client.Datagram.SendPort", i16(0),
        "Client UDP send port for keepalive packets")
    ("Hyperspace.LogGc.Interval", g_i32(60000), "Check for unused BerkeleyDB "
        "log files after this much time")
    ("Hyperspace.LogGc.MaxUnusedLogs", g_i32(200), "Number of unused BerkeleyDB "
        "to keep around in case of lagging replicas")
    ("Hyperspace.Replica.Host", g_strs(), "Hostname of Hyperspace replica")
    ("Hyperspace.Replica.Port", i16(15861),
        "Port number on which Hyperspace is or should be listening for requests")
    ("Hyperspace.Replica.Replication.Port", i16(15862),
        "Hyperspace replication port ")
    ("Hyperspace.Replica.Replication.Timeout", i32(10000),
        "Hyperspace replication master dies if it doesn't receive replication acknowledgement "
        "within this period")
    ("Hyperspace.Replica.Workers", i32(20),
        "Number of Hyperspace Replica worker threads created")
    ("Hyperspace.Replica.Reactors", i32(),
        "Number of Hyperspace Master communication reactor threads created")
    ("Hyperspace.Replica.Dir", str("hyperspace"),
         "Root of hyperspace file and directory "
        "heirarchy in local filesystem (if relative path, then is relative to "
        "the Hypertable data directory root)")
    ("Hyperspace.KeepAlive.Interval", g_i32(30000),
        "Hyperspace Keepalive interval (see Chubby paper)")
    ("Hyperspace.Lease.Interval", g_i32(60000),
        "Hyperspace Lease interval (see Chubby paper)")
    ("Hyperspace.GracePeriod", g_i32(60000),
        "Hyperspace Grace period (see Chubby paper)")
    ("Hyperspace.Session.Reconnect", boo(false),
        "Reconnect to Hyperspace on session expiry")
    ("Hypertable.Directory", str("hypertable"),
        "Top-level hypertable directory name")
    ("Hypertable.Monitoring.Interval", i32(30000),
        "Monitoring statistics gathering interval (in milliseconds)")
    ("Hypertable.Monitoring.Disable", boo(false),
        "Disables the generation of monitoring statistics")
    ("Hypertable.LoadBalancer.Enable", boo(true),
        "Enable automatic load balancing")
    ("Hypertable.LoadBalancer.Crontab", str("0 0 * * *"),
        "Crontab entry to control when load balancer is run")
    ("Hypertable.LoadBalancer.BalanceDelay.Initial", i32(86400),
        "Amount of time to wait after start up before running balancer")
    ("Hypertable.LoadBalancer.BalanceDelay.NewServer", i32(60),
        "Amount of time to wait before running balancer when a new RangeServer is detected")
    ("Hypertable.LoadBalancer.LoadavgThreshold", f64(0.25),
        "Servers with loadavg above this much above the mean will be considered by the "
        "load balancer to be overloaded")
    ("Hypertable.HqlInterpreter.Mutator.NoLogSync", boo(false),
        "Suspends CommitLog sync operation on updates until command completion")
    ("Hypertable.RangeLocator.MetadataReadaheadCount", i32(10),
        "Number of rows that the RangeLocator fetches from the METADATA")
    ("Hypertable.RangeLocator.MaxErrorQueueLength", i32(4),
        "Maximum numbers of errors to be stored")
    ("Hypertable.RangeLocator.MetadataRetryInterval", i32(3000),
        "Retry interval when connecting to a RangeServer to fetch metadata")
    ("Hypertable.RangeLocator.RootMetadataRetryInterval", i32(3000),
        "Retry interval when connecting to the Root RangeServer")
    ("Hypertable.Mutator.FlushDelay", i32(0), "Number of "
        "milliseconds to wait prior to flushing scatter buffers (for testing)")
    ("Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer",
     i32(10*M), "Amount of updates (bytes) accumulated for a "
        "single server to trigger a scatter buffer flush")
    ("Hypertable.Mutator.ScatterBuffer.FlushLimit.Aggregate",
     i64(50*M), "Amount of updates (bytes) accumulated for "
        "all servers to trigger a scatter buffer flush")
    ("Hypertable.Scanner.QueueSize", i32(5), "Size of Scanner ScanBlock queue")
    ("Hypertable.LocationCache.MaxEntries", i64(1*M),
        "Size of range location cache in number of entries")
    ("Hypertable.Master.Host", str(),
        "Host on which Hypertable Master is running")
    ("Hypertable.Master.Port", i16(15864),
        "Port number on which Hypertable Master is or should be listening")
    ("Hypertable.Master.Workers", i32(100),
        "Number of Hypertable Master worker threads created")
    ("Hypertable.Master.Reactors", i32(),
        "Number of Hypertable Master communication reactor threads created")
    ("Hypertable.Master.Gc.Interval", i32(300000),
        "Garbage collection interval in milliseconds by Master")
    ("Hypertable.Master.Locations.IncludeMasterHash", boo(false),
        "Includes master hash (host:port) in RangeServer location id")
    ("Hypertable.Master.Split.SoftLimitEnabled", boo(true),
        "Enable aggressive splitting of tables with little data to spread out ranges")
    ("Hypertable.Master.DiskThreshold.Percentage", i32(90),
        "Stop assigning ranges to RangeServers if disk usage is above this threshold")
    ("Hypertable.Master.FailedRangeServerLimit.Percentage", i32(80),
        "Fail hard if less than this percentage of the RangeServers are unavailable "
        "at a given time")
    ("Hypertable.Master.NotificationInterval", i32(3600),
        "Notification interval (in seconds) of abnormal state")
    ("Hypertable.Master.RecordGraphvizStream", boo(false),
     "Appends Graphviz output to run/op-graphviz-stream on each DAG change")
    ("Hypertable.Failover.GracePeriod", i32(30000),
        "Master wait this long before trying to recover a RangeServer")
    ("Hypertable.Failover.Timeout", i32(300000),
        "Timeout for failover operations")
    ("Hypertable.Failover.Quorum.Percentage", i32(90),
        "Percentage of live RangeServers required for failover to proceed")
    ("Hypertable.Failover.RecoverInSeries", boo(false),
        "Carry out USER log recovery for failed servers in series")
    ("Hypertable.RangeServer.AccessGroup.GarbageThreshold.Percentage",
     i32(20), "Perform major compaction when garbage accounts "
     "for this percentage of the data")
    ("Hypertable.RangeServer.ControlFile.CheckInterval", i32(30000),
     "Minimum time interval (milliseconds) to check for control files in run/ directory")
    ("Hypertable.RangeServer.LoadSystemTablesOnly", boo(false),
        "Instructs the RangeServer to only load system tables (for debugging)")
    ("Hypertable.RangeServer.LowActivityPeriod", strs(Strings()),
     "Periods of low activity during which RangeServer can perform heavy "
     "maintenance (specified in crontab format)")
    ("Hypertable.RangeServer.MemoryLimit", i64(), "RangeServer memory limit")
    ("Hypertable.RangeServer.MemoryLimit.Percentage", i32(60),
     "RangeServer memory limit specified as percentage of physical RAM")
    ("Hypertable.RangeServer.LowMemoryLimit.Percentage", i32(10),
     "Amount of memory to free in low memory condition as percentage of RangeServer memory limit")
    ("Hypertable.RangeServer.MemoryLimit.EnsureUnused", i64(), "Amount of unused physical memory")
    ("Hypertable.RangeServer.MemoryLimit.EnsureUnused.Percentage", i32(),
     "Amount of unused physical memory specified as percentage of physical RAM")
    ("Hypertable.RangeServer.Port", i16(15865),
        "Port number on which range servers are or should be listening")
    ("Hypertable.RangeServer.AccessGroup.CellCache.PageSize",
     i32(512*KiB), "Page size for CellCache pool allocator")
    ("Hypertable.RangeServer.AccessGroup.CellCache.ScannerCacheSize",
     i32(1024), "CellCache scanner cache size")
    ("Hypertable.RangeServer.AccessGroup.ShadowCache",
     boo(false), "Enable CellStore shadow caching")
    ("Hypertable.RangeServer.AccessGroup.MaxMemory", i64(1*G),
        "Maximum bytes consumed by an Access Group")
    ("Hypertable.RangeServer.CellStore.TargetSize.Minimum", i64(10*MiB),
     "Merging compaction target CellStore size during normal activity period")
    ("Hypertable.RangeServer.CellStore.TargetSize.Maximum", i64(50*MiB),
     "Merging compaction target CellStore size during low activity period")
    ("Hypertable.RangeServer.CellStore.TargetSize.Window", i64(30*MiB), 
     "Size window above target minimum for CellStores in which merges will be considered")
    ("Hypertable.RangeServer.CellStore.Merge.RunLengthThreshold", i32(5), 
     "Trigger a merge if an adjacent run of merge candidate CellStores exceeds this length")
    ("Hypertable.RangeServer.CellStore.DefaultBlockSize",
        i32(64*KiB), "Default block size for cell stores")
    ("Hypertable.RangeServer.Data.DefaultReplication",
        i32(-1), "Default replication for data")
    ("Hypertable.RangeServer.CellStore.DefaultCompressor",
        str("snappy"), "Default compressor for cell stores")
    ("Hypertable.RangeServer.CellStore.DefaultBloomFilter",
        str("rows"), "Default bloom filter for cell stores")
    ("Hypertable.RangeServer.CellStore.SkipBad",
        boo(false), "Skip over cell stores that are corrupt")
    ("Hypertable.RangeServer.CellStore.SkipNotFound",
        boo(false), "Skip over cell stores that are non-existent")
    ("Hypertable.RangeServer.IgnoreClockSkewErrors",
        boo(false), "Ignore clock skew errors")
    ("Hypertable.RangeServer.CommitInterval", i32(50),
     "Default minimum group commit interval in milliseconds")
    ("Hypertable.RangeServer.BlockCache.Compressed", boo(true),
        "Controls whether or not block cache stores compressed blocks")
    ("Hypertable.RangeServer.BlockCache.MinMemory", i64(0),
        "Minimum size of block cache")
    ("Hypertable.RangeServer.BlockCache.MaxMemory", i64(-1),
        "Maximum (target) size of block cache")
    ("Hypertable.RangeServer.QueryCache.EnableMutexStatistics",
     boo(true), "Enable query cache mutex statistics")
    ("Hypertable.RangeServer.QueryCache.MaxMemory", i64(50*M),
        "Maximum size of query cache")
    ("Hypertable.RangeServer.Location.AutoReInitiate", boo(false),
     "If RS location marked removed, deletes previous location and inititate a new RS location")
    ("Hypertable.RangeServer.Range.RowSize.Unlimited", boo(false),
     "Marks range active and unsplittable upon encountering row overflow condition. "
     "Can cause ranges to grow extremely large.  Use with caution!")
    ("Hypertable.RangeServer.Range.IgnoreCellsWithClockSkew", boo(false),
     "Forces Ranges to ingore inserted cells whose revision number is older than latest"
     "revision found in CellStores.")
    ("Hypertable.RangeServer.Range.SplitSize", i64(512*MiB),
        "Size of range in bytes before splitting")
    ("Hypertable.RangeServer.Range.MaximumSize", i64(3*G),
        "Maximum size of a range in bytes before updates get throttled")
    ("Hypertable.RangeServer.Range.MetadataSplitSize", i64(), "Size of METADATA "
        "range in bytes before splitting (for testing)")
    ("Hypertable.RangeServer.Range.SplitOff", str("high"),
        "Portion of range to split off (high or low)")
    ("Hypertable.RangeServer.ClockSkew.Max", i32(3*M),
        "Maximum amount of clock skew (microseconds) the system will tolerate")
    ("Hypertable.RangeServer.CommitLog.FsBroker.Host", str(),
        "Host of DFS Broker to use for Commit Log")
    ("Hypertable.RangeServer.CommitLog.FsBroker.Port", i16(),
        "Port of DFS Broker to use for Commit Log")
    ("Hypertable.RangeServer.CommitLog.FragmentRemoval.RangeReferenceRequired", boo(true),
        "Only remove linked log fragments if they're part of a transfer log referenced by a range")
    ("Hypertable.RangeServer.CommitLog.PruneThreshold.Min", i64(1*G),
        "Lower threshold for amount of outstanding commit log before pruning")
    ("Hypertable.RangeServer.CommitLog.PruneThreshold.Max", i64(),
        "Upper threshold for amount of outstanding commit log before pruning")
    ("Hypertable.RangeServer.CommitLog.PruneThreshold.Max.MemoryPercentage",
        i32(50), "Upper threshold in terms of % RAM for "
        "amount of outstanding commit log before pruning")
    ("Hypertable.RangeServer.CommitLog.RollLimit", i64(100*M),
        "Roll commit log after this many bytes")
    ("Hypertable.RangeServer.CommitLog.Compressor",
        str("quicklz"),
       "Commit log compressor to use (zlib, lzo, quicklz, snappy, bmz, zstd, none)")
    ("Hypertable.RangeServer.Testing.MaintenanceNeeded.PauseInterval", i32(0),
        "TESTING:  After update, if range needs maintenance, pause for this number of milliseconds")
    ("Hypertable.RangeServer.UpdateCoalesceLimit", i64(5*M),
        "Amount of update data to coalesce into single commit log sync")
    ("Hypertable.RangeServer.Failover.FlushLimit.PerRange",
     i32(10*M), "Amount of updates (bytes) accumulated for a "
        "single range to trigger a replay buffer flush")
    ("Hypertable.RangeServer.Failover.FlushLimit.Aggregate",
     i64(100*M), "Amount of updates (bytes) accumulated for "
        "all range to trigger a replay buffer flush")
    ("Hypertable.RangeServer.ReadyStatus", str("WARNING"),
        "Status code indicating RangeServer is ready for operation")
    ("Hypertable.Metadata.Replication", i32(-1),
        "Replication factor for commit log files")
    ("Hypertable.CommitLog.RollLimit", i64(100*M),
        "Roll commit log after this many bytes")
    ("Hypertable.CommitLog.Compressor", str("quicklz"),
        "Commit log compressor to use (zlib, lzo, quicklz, snappy, bmz, zstd, none)")
    ("Hypertable.CommitLog.SkipErrors", boo(false),
        "Skip over any corruption encountered in the commit log")
    ("Hypertable.RangeServer.Scanner.Ttl", i32(1800*K),
        "Number of milliseconds of inactivity before destroying scanners")
    ("Hypertable.RangeServer.Scanner.BufferSize", i64(1*M),
        "Size of transfer buffer for scan results")
    ("Hypertable.RangeServer.Timer.Interval", i32(20000),
        "Timer interval in milliseconds (reaping scanners, purging commit logs, etc.)")
    ("Hypertable.RangeServer.Maintenance.Interval", i32(30000),
        "Maintenance scheduling interval in milliseconds")
    ("Hypertable.RangeServer.Maintenance.LowMemoryPrioritization", boo(true),
        "Use low memory prioritization algorithm for freeing memory in low memory mode")
    ("Hypertable.RangeServer.Maintenance.MaxAppQueuePause", i32(120000),
        "Each time application queue is paused, keep it paused for no more than this many milliseconds")
    ("Hypertable.RangeServer.Maintenance.MergesPerInterval", i32(),
        "Limit on number of merging tasks to create per maintenance interval")
    ("Hypertable.RangeServer.Maintenance.MergingCompaction.Delay", i32(900000),
        "Millisecond delay before scheduling merging compactions in non-low memory mode")
    ("Hypertable.RangeServer.Maintenance.MoveCompactionsPerInterval", i32(2),
        "Limit on number of major compactions due to move per maintenance interval")
    ("Hypertable.RangeServer.Maintenance.InitializationPerInterval", i32(),
        "Limit on number of initialization tasks to create per maintenance interval")
    ("Hypertable.RangeServer.Monitoring.DataDirectories", str("/"),
        "Comma-separated list of directory mount points of disk volumes to monitor")
    ("Hypertable.RangeServer.Workers", i32(50),
        "Number of Range Server worker threads created")
    ("Hypertable.RangeServer.Reactors", i32(),
        "Number of Range Server communication reactor threads created")
    ("Hypertable.RangeServer.MaintenanceThreads", i32(),
        "Number of maintenance threads.  Default is min(2, number-of-cores).")
    ("Hypertable.RangeServer.UpdateDelay", i32(0),
        "Number of milliseconds to wait before carrying out an update (TESTING)")
    ("Hypertable.RangeServer.ProxyName", str(""),
        "Use this value for the proxy name (if set) instead of reading from run dir.")
    ("ThriftBroker.Timeout", i32(), "Timeout (ms) for thrift broker")
    ("ThriftBroker.Host", str("localhost"),
     "Host on which the ThriftBroker is running (read by clients only)")
    ("ThriftBroker.Port", i16(15867), "Port number for "
        "thrift broker")
    ("ThriftBroker.Future.Capacity", i32(50*M), "Capacity "
        "of result queue (in bytes) for Future objects")
    ("ThriftBroker.NextThreshold", i32(512*K), "Total size  "
        "threshold for (size of cell data) for thrift broker next calls")
    ("ThriftBroker.API.Logging", boo(false), "Enable or "
        "disable Thrift API logging")
    ("ThriftBroker.Mutator.FlushInterval", i32(1000),
        "Maximum flush interval in milliseconds")
    ("ThriftBroker.Workers", i32(50), "Number of "
        "worker threads for thrift broker")
    ("ThriftBroker.Hyperspace.Session.Reconnect", boo(true),
        "ThriftBroker will reconnect to Hyperspace on session expiry")
    ("ThriftBroker.SlowQueryLog.Enable", g_boo(true),
        "Enable slow query logging")
    ("ThriftBroker.SlowQueryLog.LatencyThreshold", g_i32(10000),
        "Latency threshold above which a query is considered slow")
	("ThriftBroker.Transport", str("framed"),
		"Thrift Broker transport - framed/zlib")
    ;
  alias("Hypertable.RangeServer.CommitLog.RollLimit",
        "Hypertable.CommitLog.RollLimit");
  alias("Hypertable.RangeServer.CommitLog.Compressor",
        "Hypertable.CommitLog.Compressor");
  // add config file desc to cmdline hidden desc, so people can override
  // any config values on the command line
  cmdline_hidden_desc().add(file_desc());
}

void DefaultPolicy::init() {
  gEnumExtPtr loglevel = properties->get_ptr<gEnumExt>("logging-level");
  bool verbose = properties->get<gBool>("verbose");

  if (verbose && properties->get_bool("quiet")) {
    verbose = false;
    properties->set("verbose", (gBool)false);
  }
  if (properties->get_bool("debug")) {
    loglevel->set_value(Logger::Priority::DEBUG);
  }
  if(loglevel->get()==-1){
    HT_ERROR_OUT << "unknown logging level: "<< loglevel->str() << HT_END;
    std::quick_exit(EXIT_SUCCESS);
  }
  
  Logger::get()->set_level(loglevel->get());
  loglevel->set_cb_on_chg([](int value){Logger::get()->set_level(value);});

  if (verbose) {
    HT_NOTICE_OUT << "Initializing " << System::exe_name << " (Hypertable "
        << version_string() << ")..." << HT_END;
  }
}


}} // namespace Hypertable::Config
