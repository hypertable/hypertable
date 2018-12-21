add_test(RangeServer-row-overflow-1 env INSTALL_DIR=${INSTALL_DIR}
         PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py 
         ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-row-overflow-2 env INSTALL_DIR=${INSTALL_DIR}
         PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py 
         ${CMAKE_CURRENT_SOURCE_DIR}/run.sh --disabled)
