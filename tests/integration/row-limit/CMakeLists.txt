add_test(RowLimit env INSTALL_DIR=${INSTALL_DIR}
         PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py 
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)
