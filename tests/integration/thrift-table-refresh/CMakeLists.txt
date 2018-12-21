
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/thrift-table-refresh.golden
               ${CMAKE_CURRENT_BINARY_DIR}/thrift-table-refresh.golden)

add_test(Thrift-table-refresh env INSTALL_DIR=${INSTALL_DIR}
         ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
