
set(CMAKE_CXX_FLAGS "-DHAVE_NETINET_IN_H ${CMAKE_CXX_FLAGS}")

add_library(pyht MODULE HypertableBindings.cc)


target_link_libraries(pyht HyperCommon  Hypertable ${PYTHON_LIBRARY} ${LibEvent_LIBS} ${BOOST_PYTHON_LIB})
target_include_directories(pyht PRIVATE ${PYTHON_INCLUDE_DIR})
	
install(TARGETS pyht
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib/py/hypertable"
        ARCHIVE DESTINATION "lib/py/hypertable")



if (Pybind11_FOUND AND PyPy_FOUND)
	add_library(pypyht MODULE HypertableBindings.cc)
	target_link_libraries(pypyht HyperCommon Hypertable ${PYPY_LIBDIR})
	target_compile_options(pypyht PRIVATE
					  -flto
					  -DPYTHON_VERSION=${PYTHON_VERSION}
					  -DPYPY_VERSION=1
					  -DWITH_THREAD
					  )
	 target_include_directories(pypyht PRIVATE ${Pybind11_INCLUDE_DIR} ${PYPY_INCLUDE_DIR})
	
	install(TARGETS pypyht
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib/py/hypertable"
        ARCHIVE DESTINATION "lib/py/hypertable")

endif ()