add_test(Move-master-failure env INSTALL_DIR=${INSTALL_DIR}
         PYTHONPATH=${HYPERTABLE_SOURCE_DIR}/src/py/ThriftClient:${HYPERTABLE_SOURCE_DIR}/src/py/ThriftClient/gen-py
         ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
