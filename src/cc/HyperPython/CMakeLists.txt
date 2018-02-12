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


if (PYTHON2_FOUND)
	add_library(HyperPython2 MODULE SerializedCells.cc)
	target_link_libraries(HyperPython2 HyperThrift HyperCommon  Hypertable ${PYTHON2_LIBRARY} ${Thrift_LIBS} ${LibEvent_LIBS})
						  target_include_directories(HyperPython2 PRIVATE ${PYTHON2_INCLUDE_DIR})
	
	install(TARGETS HyperPython2
			RUNTIME DESTINATION bin
			LIBRARY DESTINATION "lib/py/hypertable"
			ARCHIVE DESTINATION "lib/py/hypertable")

	add_test(HyperPython2-reader env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-reader.sh python ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPython2.so)
	add_test(HyperPython2-writer env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-writer.sh python ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPython2.so)
endif ()

if (PYTHON3_FOUND)
	add_library(HyperPython3 MODULE SerializedCells.cc)
	target_link_libraries(HyperPython3 HyperThrift HyperCommon  Hypertable ${PYTHON3_LIBRARY} ${Thrift_LIBS} ${LibEvent_LIBS})
						  target_include_directories(HyperPython3 PRIVATE ${PYTHON3_INCLUDE_DIR})
	
	install(TARGETS HyperPython3
			RUNTIME DESTINATION bin
			LIBRARY DESTINATION "lib/py/hypertable"
			ARCHIVE DESTINATION "lib/py/hypertable")

	add_test(HyperPython3-reader env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-reader.sh python ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPython3.so)
	add_test(HyperPython3-writer env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-writer.sh python ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPython3.so)
endif ()




if (PYPY2_FOUND AND Pybind11_FOUND)
	add_library(HyperPyPy2 MODULE SerializedCells_pyBind11.cc)
	target_link_libraries(HyperPyPy2 HyperThrift HyperCommon Hypertable ${PYPY2_LIBDIR} ${Thrift_LIBS} ${LibEvent_LIBS})
	target_compile_options(HyperPyPy2 PRIVATE
					  -flto
					  -DPYTHON_VERSION=2
					  -DPYPY_VERSION=1
					  -DWITH_THREAD
					  )
	 target_include_directories(HyperPyPy2 PRIVATE ${Pybind11_INCLUDE_DIR} ${PYPY2_INCLUDE_DIR})
	
	install(TARGETS HyperPyPy2
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib/py/hypertable"
        ARCHIVE DESTINATION "lib/py/hypertable")

	add_test(HyperPyPy2-reader env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-reader.sh pypy ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPyPy2.so)
	add_test(HyperPyPy2-writer env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-writer.sh pypy ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPyPy2.so)

endif ()

if (PYPY3_FOUND AND Pybind11_FOUND)
	add_library(HyperPyPy3 MODULE SerializedCells_pyBind11.cc)
	target_link_libraries(HyperPyPy3 HyperThrift HyperCommon Hypertable ${PYPY3_LIBDIR} ${Thrift_LIBS} ${LibEvent_LIBS})
	target_compile_options(HyperPyPy3 PRIVATE
					  -flto
					  -DPYTHON_VERSION=3
					  -DPYPY_VERSION=1
					  -DWITH_THREAD
					  )
	 target_include_directories(HyperPyPy3 PRIVATE ${Pybind11_INCLUDE_DIR} ${PYPY3_INCLUDE_DIR})
	
	install(TARGETS HyperPyPy3
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION "lib/py/hypertable"
        ARCHIVE DESTINATION "lib/py/hypertable")

	add_test(HyperPyPy3-reader env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-reader.sh pypy ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPyPy3.so)
	add_test(HyperPyPy3-writer env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run-writer.sh pypy ${CMAKE_INSTALL_PREFIX}/lib/py/hypertable/libHyperPyPy3.so)

endif ()

