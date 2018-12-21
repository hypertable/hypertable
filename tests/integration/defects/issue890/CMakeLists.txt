if (Thrift_FOUND)
	add_test(Defects-issue890 env INSTALL_DIR=${INSTALL_DIR}
			${CMAKE_CURRENT_SOURCE_DIR}/run.sh ${VERSION} ${THRIFT_VERSION})
endif ()