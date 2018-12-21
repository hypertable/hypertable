if (TARGET ThriftClient-cpp)
	add_test(ThriftClient-reconnect-hyperspace env
			THRIFT_CPP_TEST_DIR=${HYPERTABLE_BINARY_DIR}/src/cc/ThriftBroker/  
			INSTALL_DIR=${INSTALL_DIR}
			TEST_BIN=ThriftClient-cpp
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
endif()
if (TARGET testShared-ThriftClient-cpp)
	add_test(ThriftClient-reconnect-hyperspace-testShared env
			THRIFT_CPP_TEST_DIR=${HYPERTABLE_BINARY_DIR}/src/cc/ThriftBroker/  
			INSTALL_DIR=${INSTALL_DIR}
			TEST_BIN=testShared-ThriftClient-cpp
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
endif()
if (TARGET testStatic-ThriftClient-cpp)
	add_test(ThriftClient-reconnect-hyperspace-testStatic env
			THRIFT_CPP_TEST_DIR=${HYPERTABLE_BINARY_DIR}/src/cc/ThriftBroker/  
			INSTALL_DIR=${INSTALL_DIR}
			TEST_BIN=testStatic-ThriftClient-cpp
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
endif()


if (TARGET ThriftClientZ-cpp)
	add_test(ThriftClientZ-reconnect-hyperspace env
			THRIFT_CPP_TEST_DIR=${HYPERTABLE_BINARY_DIR}/src/cc/ThriftBroker/  
			INSTALL_DIR=${INSTALL_DIR}
			TEST_BIN=ThriftClientZ-cpp
			HT_TEST_THRIFT_TP=zlib
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
endif()
if (TARGET testShared-ThriftClientZ-cpp)
	add_test(ThriftClientZ-reconnect-hyperspace-testShared env
			THRIFT_CPP_TEST_DIR=${HYPERTABLE_BINARY_DIR}/src/cc/ThriftBroker/  
			INSTALL_DIR=${INSTALL_DIR}
			TEST_BIN=testShared-ThriftClientZ-cpp
			HT_TEST_THRIFT_TP=zlib
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
endif()
if (TARGET testStatic-ThriftClientZ-cpp)
	add_test(ThriftClientZ-reconnect-hyperspace-testStatic env
			THRIFT_CPP_TEST_DIR=${HYPERTABLE_BINARY_DIR}/src/cc/ThriftBroker/  
			INSTALL_DIR=${INSTALL_DIR}
			TEST_BIN=testStatic-ThriftClientZ-cpp
			HT_TEST_THRIFT_TP=zlib
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
endif()

