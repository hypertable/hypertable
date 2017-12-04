#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Common")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Common")

if (APPLE)
  set(Fmemopen_SRCS fmemopen.c)
endif ()

set(Common_SRCS
Base64.cc
Checksum.cc
ClusterDefinition.cc
ClusterDefinitionFile/Compiler.cc
ClusterDefinitionFile/ToJson.cc
ClusterDefinitionFile/Token.cc
ClusterDefinitionFile/Tokenizer.cc
ClusterDefinitionFile/TokenizerTools.cc
ClusterDefinitionFile/TranslatorCode.cc
ClusterDefinitionFile/TranslatorRole.cc
ClusterDefinitionFile/TranslatorTask.cc
ClusterDefinitionFile/TranslatorVariable.cc
Config.cc
ConsoleOutputSquelcher.cc
Cronolog.cc
Crontab.cc
Crypto.cc
DiscreteRandomGenerator.cc
DiscreteRandomGeneratorFactory.cc
DiscreteRandomGeneratorZipf.cc
Error.cc
Escaper.cc
FailureInducer.cc
fast_clock.cc
FileUtils.cc
Filesystem.cc
HostSpecification.cc
InetAddr.cc
InteractiveCommand.cc
Logger.cc
MetricsCollectorGanglia.cc
MetricsProcess.cc
MurmurHash.cc
Properties.cc
Random.cc
Regex.cc
Serializable.cc
SleepWakeNotifier.cc
Status.cc
StatusPersister.cc
String.cc
System.cc
SystemInfo.cc
StatsSerializable.cc
StatsSystem.cc
Time.cc
TimeWindow.cc
Usage.cc
Version.cc
WordStream.cc
XmlParser.cc
md5.cc
)

if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  link_libraries(socket)
endif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")

add_library(HyperCommon ${Common_SRCS} ${Fmemopen_SRCS})
target_link_libraries(HyperCommon ${EXPAT_LIBRARIES} ${SIGAR_LIBRARIES}
  ${BOOST_LIBS} ${EDITLINE_LIBRARIES} ${ZLIB_LIBRARIES} ${SNAPPY_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT} ${RE2_LIBRARIES} ${MALLOC_LIBRARY} ${Libssl_LIBRARIES})

add_executable(ht_system_info system_info.cc)
target_link_libraries(ht_system_info HyperCommon)

add_executable(ht_wordstream ht_wordstream.cc)
target_link_libraries(ht_wordstream HyperCommon)

# tests
add_executable(Base64_test tests/Base64_test.cc)
target_link_libraries(Base64_test HyperCommon)

add_executable(Crypto_test tests/Crypto_test.cc)
target_link_libraries(Crypto_test HyperCommon)

add_executable(directory_test tests/directory_test.cc)
target_link_libraries(directory_test HyperCommon)

add_executable(exception_test tests/exception_test.cc)
target_link_libraries(exception_test HyperCommon)

add_executable(escaper_test tests/escaper_test.cc)
target_link_libraries(escaper_test HyperCommon)

add_executable(HostSpecification_test tests/HostSpecification_test.cc)
target_link_libraries(HostSpecification_test HyperCommon)
configure_file(${SRC_DIR}/tests/HostSpecification_test.golden
               ${DST_DIR}/HostSpecification_test.golden)

add_executable(ClusterDefinition_test tests/ClusterDefinition_test.cc)
target_link_libraries(ClusterDefinition_test HyperCommon)
configure_file(${SRC_DIR}/tests/ClusterDefinition_test.golden
               ${DST_DIR}/ClusterDefinition_test.golden)

add_executable(logging_test tests/logging_test.cc)
target_link_libraries(logging_test HyperCommon)

add_subdirectory(ClusterDefinitionFile)

# serialization tests
add_executable(Serializable_test tests/Serializable_test.cc)
target_link_libraries(Serializable_test HyperCommon)

add_executable(sertest tests/sertest.cc)
target_link_libraries(sertest HyperCommon)

# scope guard tests
add_executable(scope_guard_test tests/scope_guard_test.cc)
target_link_libraries(scope_guard_test HyperCommon)

# inetaddr tests
add_executable(inetaddr_test tests/inetaddr_test.cc)
if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  target_link_libraries(inetaddr_test HyperCommon nsl resolv)
else (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  target_link_libraries(inetaddr_test HyperCommon)
endif (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")

# pagearena tests
add_executable(pagearena_test tests/pagearena_test.cc)
target_link_libraries(pagearena_test HyperCommon ${MALLOC_LIBRARY})

# mutex tests
add_executable(mutex_test tests/mutex_test.cc)
target_link_libraries(mutex_test HyperCommon)

# properties tests
add_executable(properties_test tests/properties_test.cc)
target_link_libraries(properties_test HyperCommon)

# config tests
add_executable(config_test tests/config_test.cc)
target_link_libraries(config_test HyperCommon)

# crontab tests
add_executable(crontab_test tests/crontab_test.cc)
target_link_libraries(crontab_test HyperCommon)

# TimeWindow test
add_executable(TimeWindowTest tests/TimeWindowTest.cc)
target_link_libraries(TimeWindowTest HyperCommon)

# init tests
add_executable(init_test tests/init_test.cc)
target_link_libraries(init_test HyperCommon)

# filesystem tests
add_executable(filesystem_test tests/filesystem_test.cc)
target_link_libraries(filesystem_test HyperCommon)

# macro expanded formatted sertest.cc for easy debugging
# sertest-x.cc is generated by gpp included in toplevel bin/gpp
#add_executable(sertestx tests/sertest-x.cc)
#target_link_libraries(sertestx HyperCommon)

# bloom filter test
add_executable(bloom_filter_test tests/bloom_filter_test.cc)
target_link_libraries(bloom_filter_test HyperCommon)

# hash test
add_executable(hash_test tests/hash_test.cc)
target_link_libraries(hash_test HyperCommon ${MALLOC_LIBRARY})

# timeinline test
add_executable(timeinline_test tests/timeinline_test.cc)
target_link_libraries(timeinline_test HyperCommon ${MALLOC_LIBRARY})

# container tests
add_executable(container_test tests/container_test.cc)
target_link_libraries(container_test HyperCommon)

# md5 base 64 test
add_executable(md5_base64_test tests/md5_base64_test.cc)
target_link_libraries(md5_base64_test HyperCommon)

# StatsSerializable test
add_executable(stats_serialize_test tests/stats_serialize_test.cc)
target_link_libraries(stats_serialize_test HyperCommon)

# StringCompressor test
add_executable(string_compressor_test tests/string_compressor_test.cc)
target_link_libraries(string_compressor_test HyperCommon)

# FailureInducer test
add_executable(failure_inducer_test tests/failure_inducer_test.cc)
target_link_libraries(failure_inducer_test HyperCommon)
configure_file(${SRC_DIR}/tests/failure_inducer_test.golden
               ${DST_DIR}/failure_inducer_test.golden)

add_test(Common-Exception exception_test)
add_test(Common-Exception escaper_test)
add_test(Common-HostSpecification HostSpecification_test)
add_test(Common-ClusterDefinition ClusterDefinition_test)
add_test(Common-Logging logging_test)
add_test(Common-Serializable Serializable_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/Serializable_test.golden)
add_test(Common-Serialization sertest)
add_test(Common-ScopeGuard scope_guard_test)
add_test(Common-InetAddr inetaddr_test)
add_test(Common-PageArena pagearena_test)
add_test(Common-Config config_test)
add_test(Common-Crontab env bash -c "${CMAKE_CURRENT_BINARY_DIR}/crontab_test > crontab_test.output; diff crontab_test.output ${CMAKE_CURRENT_SOURCE_DIR}/tests/crontab_test.golden")
add_test(Common-Base64 Base64_test)
add_test(Common-Crypto Crypto_test)
add_test(Common-directory directory_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/directory_test.golden)
add_test(Common-Properties ${TEST_DIFF}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/properties_test.golden ./properties_test)
add_test(Common-init ${TEST_DIFF}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/init_test.golden
  ./init_test --i16 1k --i32 64K --i64 1G --boo)
add_test(MD5-Base64 md5_base64_test)
add_test(Common-StatsSystem-serialize stats_serialize_test)
add_test(Common-StringCompressor string_compressor_test)
add_test(Common-TimeInline timeinline_test)
add_test(Common-TimeWindow env bash -c "${CMAKE_CURRENT_BINARY_DIR}/TimeWindowTest > TimeWindowTest.output; diff TimeWindowTest.output ${CMAKE_CURRENT_SOURCE_DIR}/tests/TimeWindowTest.golden")
add_test(Common-FailureInducer failure_inducer_test)

set(VERSION_H ${HYPERTABLE_BINARY_DIR}/src/cc/Common/Version.h)

configure_file(Version.h.in ${VERSION_H})
configure_file(${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
               ${HYPERTABLE_BINARY_DIR}/src/cc/Common/words.gz COPYONLY)
add_test(Common-BloomFilter bloom_filter_test)
add_test(Common-Hash hash_test)

if (NOT HT_COMPONENT_INSTALL)
  file(GLOB HEADERS *.h metrics)

  install(FILES ${HEADERS} ${VERSION_H} DESTINATION include/Common)

  install(TARGETS HyperCommon
          RUNTIME DESTINATION bin
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib)

  install(TARGETS ht_system_info
          RUNTIME DESTINATION bin)
endif ()
