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

HT_ADD_LIBS(
	TARGET HyperCommon 
	SRCS ${Common_SRCS} ${Fmemopen_SRCS}
	TARGETS HyperThirdParty
	DEPENDENCIES ${CMAKE_THREAD_LIBS_INIT} ${MALLOC_LIBRARY} ${BOOST_LIBS} ${ZLIB_LIBRARIES} ${BZIP2_LIBRARIES} ${LZMA_LIBRARIES}
				 ${TINFOW_LIBRARIES} 
				 ${RE2_LIBRARIES} ${CRYPTO_LIBRARIES})

add_executable(ht_system_info system_info.cc)
target_link_libraries(ht_system_info HyperCommon)

add_executable(ht_wordstream ht_wordstream.cc)
target_link_libraries(ht_wordstream HyperCommon)
configure_file(${HYPERTABLE_SOURCE_DIR}/tests/data/words.gz
               ${HYPERTABLE_BINARY_DIR}/src/cc/Common/words.gz COPYONLY)

# tests
set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Common")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Common")

HT_ADD_TEST(
	NAME Common-Base64
	SRCS tests/Base64_test.cc
	TARGETS  Hypertable
)
HT_ADD_TEST(
	NAME Common-Crypto
	SRCS tests/Crypto_test.cc
	TARGETS Hypertable 
)
HT_ADD_TEST(
	NAME Common-Exception
	SRCS tests/exception_test.cc
	TARGETS Hypertable 
)
HT_ADD_TEST(
	NAME Common-directory
	SRCS tests/directory_test.cc
	TARGETS Hypertable 
	ARGS ${CMAKE_CURRENT_SOURCE_DIR}/tests/directory_test.golden
)
HT_ADD_TEST(
	NAME Common-Escaper
	SRCS tests/escaper_test.cc
	TARGETS Hypertable 
)
configure_file(${SRC_DIR}/tests/HostSpecification_test.golden
               ${DST_DIR}/HostSpecification_test.golden)
HT_ADD_TEST(
	NAME Common-HostSpecification
	SRCS tests/HostSpecification_test.cc
	TARGETS Hypertable 
)
# ClusterDefinition tests
configure_file(${SRC_DIR}/tests/ClusterDefinition_test.golden
               ${DST_DIR}/ClusterDefinition_test.golden)
HT_ADD_TEST(
	NAME Common-ClusterDefinition
	SRCS tests/ClusterDefinition_test.cc
	TARGETS Hypertable 
)
add_subdirectory(ClusterDefinitionFile)
HT_ADD_TEST(
	NAME Common-Logging
	SRCS tests/logging_test.cc
	TARGETS Hypertable 
)
# serialization tests
HT_ADD_TEST(
	NAME Common-Serializable
	SRCS tests/Serializable_test.cc
	TARGETS Hypertable 
	ARGS ${CMAKE_CURRENT_SOURCE_DIR}/tests/Serializable_test.golden
)
HT_ADD_TEST(
	NAME Common-Serialization
	SRCS tests/sertest.cc
	TARGETS Hypertable 
)
# scope guard tests
HT_ADD_TEST(
	NAME Common-ScopeGuard
	SRCS tests/scope_guard_test.cc
	TARGETS Hypertable 
)
# inetaddr tests
set(ext_deps )
if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
  set(ext_deps nsl resolv)
endif ()
HT_ADD_TEST(
	NAME Common-InetAddr
	SRCS tests/inetaddr_test.cc
	TARGETS Hypertable 
	EXT_DEPS ${ext_deps}
)

# pagearena tests
HT_ADD_TEST(
	NAME Common-PageArena
	SRCS tests/pagearena_test.cc
	TARGETS Hypertable 
)
# config tests
HT_ADD_TEST(
	NAME Common-Config
	SRCS tests/config_test.cc
	TARGETS Hypertable 
)

# crontab tests
add_executable(crontab_test tests/crontab_test.cc)
target_link_libraries(crontab_test HyperCommon)
add_test(Common-Crontab env 
		 bash -c "${CMAKE_CURRENT_BINARY_DIR}/crontab_test > crontab_test.output; diff crontab_test.output ${CMAKE_CURRENT_SOURCE_DIR}/tests/crontab_test.golden")

# properties tests
add_executable(properties_test tests/properties_test.cc)
target_link_libraries(properties_test HyperCommon)
add_test(Common-Properties ${TEST_DIFF}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/properties_test.golden ./properties_test)
  
# init tests
add_executable(init_test tests/init_test.cc)
target_link_libraries(init_test HyperCommon)
add_test(Common-init ${TEST_DIFF}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests/init_test.golden
  ./init_test --i16 1k --i32 64K --i64 1G --boo)
  
# md5 base 64 test
HT_ADD_TEST(
	NAME MD5-Base64
	SRCS tests/md5_base64_test.cc
	TARGETS Hypertable 
)
# StatsSerializable test
HT_ADD_TEST(
	NAME Common-StatsSystem-serialize
	SRCS tests/stats_serialize_test.cc
	TARGETS Hypertable 
)
# StringCompressor test
HT_ADD_TEST(
	NAME Common-StringCompressor
	SRCS tests/string_compressor_test.cc
	TARGETS Hypertable 
)
# timeinline test
HT_ADD_TEST(
	NAME Common-TimeInline
	SRCS tests/timeinline_test.cc
	TARGETS Hypertable 
)
# FailureInducer test
HT_ADD_TEST(
	NAME Common-FailureInducer
	SRCS tests/failure_inducer_test.cc
	TARGETS Hypertable 
)
configure_file(${SRC_DIR}/tests/failure_inducer_test.golden
               ${DST_DIR}/failure_inducer_test.golden)
# TimeWindow test
add_executable(TimeWindowTest tests/TimeWindowTest.cc)
target_link_libraries(TimeWindowTest HyperCommon)
add_test(Common-TimeWindow env bash -c "${CMAKE_CURRENT_BINARY_DIR}/TimeWindowTest > TimeWindowTest.output; diff TimeWindowTest.output ${CMAKE_CURRENT_SOURCE_DIR}/tests/TimeWindowTest.golden")

# bloom filter test
HT_ADD_TEST(
	NAME Common-BloomFilter
	SRCS tests/bloom_filter_test.cc
	TARGETS Hypertable 
)
# hash test
HT_ADD_TEST(
	NAME Common-Hash
	SRCS tests/hash_test.cc
	TARGETS Hypertable 
)
# mutex tests
HT_ADD_TEST(
	NAME Common-Mutex
	SRCS tests/mutex_test.cc
	TARGETS Hypertable 
)
# filesystem tests
HT_ADD_TEST(
	NAME Common-FileSystem
	SRCS tests/filesystem_test.cc
	TARGETS Hypertable 
)
# container tests
#HT_ADD_TEST(
#	NAME Common-Container
#	SRCS tests/container_test.cc
#	TARGETS Hypertable 
#)

# macro expanded formatted sertest.cc for easy debugging
# sertest-x.cc is generated by gpp included in toplevel bin/gpp
#add_executable(sertestx tests/sertest-x.cc)
#target_link_libraries(sertestx HyperCommon)


set(VERSION_H ${HYPERTABLE_BINARY_DIR}/src/cc/Common/Version.h)
configure_file(Version.h.in ${VERSION_H})

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
