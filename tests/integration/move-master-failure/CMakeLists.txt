add_test(Move-master-failure env INSTALL_DIR=${INSTALL_DIR}
         PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py 
         ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

