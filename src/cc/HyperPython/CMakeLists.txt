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
# Python bindings for SerializedCellsReader/SerializedCellsWriter
#

set(CMAKE_CXX_FLAGS "-DHAVE_NETINET_IN_H ${CMAKE_CXX_FLAGS}")



add_library(HyperPython MODULE SerializedCells.cc)
target_link_libraries(HyperPython HyperThrift HyperCommon  Hypertable ${PYTHON_LIBRARY} ${Thrift_LIBS} ${LibEvent_LIBS})
target_include_directories(HyperPython PRIVATE ${PYTHON_INCLUDE_DIR})
	
install(TARGETS HyperPython
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib/py/hypertable"
        ARCHIVE DESTINATION "lib/py/hypertable")

add_test(HyperPython-reader env PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/gen-py/ bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-reader.sh python ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPython.so)
add_test(HyperPython-writer env PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/gen-py/ bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-writer.sh python ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPython.so)


if (Pybind11_FOUND AND PyPy_FOUND)
	add_library(HyperPyPy MODULE SerializedCells_pyBind11.cc)
	target_link_libraries(HyperPyPy HyperThrift HyperCommon Hypertable ${PYPY_LIBDIR} ${Thrift_LIBS} ${LibEvent_LIBS})
	target_compile_options(HyperPyPy PRIVATE
					  -flto
					  -DPYTHON_VERSION=${PYTHON_VERSION}
					  -DPYPY_VERSION=1
					  -DWITH_THREAD
					  )
	 target_include_directories(HyperPyPy PRIVATE ${Pybind11_INCLUDE_DIR} ${PYPY_INCLUDE_DIR})
	
	install(TARGETS HyperPyPy
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib/py/hypertable"
        ARCHIVE DESTINATION "lib/py/hypertable")

	add_test(HyperPyPy-reader env PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/gen-py/ bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-reader.sh pypy ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPyPy.so)
	add_test(HyperPyPy-writer env PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/:${CMAKE_SOURCE_DIR}/src/py/ThriftClient/gen-py/ bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-writer.sh pypy ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPyPy.so)

endif ()
