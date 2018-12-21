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



#### PREPARE HYPERTABLE PKG DIR
add_custom_target(ThriftClientPy ALL)
add_custom_command(
  TARGET   ThriftClientPy
  COMMAND  cp 
  ARGS     -r ${HYPERTABLE_SOURCE_DIR}/src/py/ht-package/* ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg
)
configure_file(setup.py.in ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/setup.py @ONLY)
configure_file(requirements.txt.in ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/requirements.txt @ONLY)

#### GENERATE HYPERTHRIFT module
if (THRIFT_FOUND)
	add_custom_command(
		TARGET 		ThriftClientPy
		COMMAND   	thrift
		ARGS      	-r --gen py:slots,no_utf8strings -o ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/
					${ThriftBroker_IDL_DIR}/Hql.thrift
		DEPENDS   	${ThriftBroker_IDL_DIR}/Client.thrift
					${ThriftBroker_IDL_DIR}/Hql.thrift
		COMMENT   	"ThriftPyGen"
	)
		
	#### SET HYPERTHRIFT module to HYPERTABLE PKG
	add_custom_command(
		TARGET 	ThriftClientPy
		COMMAND   cp 
		ARGS      -r ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/gen-py/hypertable/thrift_client/hyperthrift ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/hypertable/thrift_client/
	)
	#### minimize attr access
	add_custom_command(
		TARGET 	 ThriftClientPy

		COMMAND  sed 
		ARGS     -i 's/import hypertable.thrift_client.hyperthrift.gen.ttypes/from hypertable.thrift_client.hyperthrift.gen import ttypes as gen_ttypes/g' ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/hypertable/thrift_client/hyperthrift/gen2/ttypes.py
	)
	add_custom_command(
		TARGET 	 ThriftClientPy
		COMMAND  sed 
		ARGS     -i 's/hypertable.thrift_client.hyperthrift.gen.ttypes./gen_ttypes./g' ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/hypertable/thrift_client/hyperthrift/gen2/ttypes.py
	)
	
endif ()



#  env PYTHONPATH=${CMAKE_INSTALL_PREFIX}/lib/py

if (PYTHON2_FOUND)
	add_test(ThriftClient-python2 env python ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test.py)
	add_test(ThriftClient-python2-scw env python ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scw.py)
	add_test(ThriftClient-python2-scr env python ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scr.py)
	add_test(HypertableClient-python2 
					env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${INSTALL_DIR}/lib 
					python ${CMAKE_CURRENT_SOURCE_DIR}/tests/ht_client_test.py)
endif ()
if (PYTHON3_FOUND)
	add_test(ThriftClient-python3 env python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test.py)
	add_test(ThriftClient-python3-scw env python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scw.py)
	add_test(ThriftClient-python3-scr env python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scr.py)
	add_test(HypertableClient-python3 
					env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${INSTALL_DIR}/lib 
					python3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/ht_client_test.py)
endif ()
if (PYPY2_FOUND)
	add_test(ThriftClient-pypy2 env pypy ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test.py)
	add_test(ThriftClient-pypy2-scw env pypy ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scw.py)
	add_test(ThriftClient-pypy2-scr env pypy ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scr.py)
	add_test(HypertableClient-pypy2 
					env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${INSTALL_DIR}/lib 
					pypy ${CMAKE_CURRENT_SOURCE_DIR}/tests/ht_client_test.py)
endif ()
if (PYPY3_FOUND)
	add_test(ThriftClient-pypy3 env pypy3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test.py)
	add_test(ThriftClient-pypy3-scw env pypy3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scw.py)
	add_test(ThriftClient-pypy3-scr env pypy3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/client_test_scr.py)
	add_test(HypertableClient-pypy3 
					env LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${INSTALL_DIR}/lib 
					pypy3 ${CMAKE_CURRENT_SOURCE_DIR}/tests/ht_client_test.py)
endif ()
	  



if(PYTHON2_FOUND OR PYTHON3_FOUND OR PYPY2_FOUND OR PYPY3_FOUND)
	install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg python setup.py sdist;)")
	if (NOT HT_COMPONENT_INSTALL)
	   install(DIRECTORY ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/dist/ DESTINATION lib/py/)
	endif ()
endif ()
	  
if (NOT HT_COMPONENT_INSTALL)
	if (PYTHON2_FOUND)
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/dist python -m pip install --upgrade hypertable-${VERSION}.tar.gz;)")
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg python -m pip install --upgrade -r requirements.txt;)")
	endif ()

	if (PYTHON3_FOUND)
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/dist python3 -m pip install --upgrade hypertable-${VERSION}.tar.gz;)")
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg python3 -m pip install --upgrade -r requirements.txt;)")
	endif ()
	if (PYPY2_FOUND)
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/dist pypy -m pip install --upgrade hypertable-${VERSION}.tar.gz;)")
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg pypy -m pip install --upgrade -r requirements.txt;)")
	endif ()
	if (PYPY3_FOUND)
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg/dist pypy3 -m pip install --upgrade hypertable-${VERSION}.tar.gz;)")
		install( CODE "execute_process(COMMAND  cmake -E chdir ${HYPERTABLE_BINARY_DIR}/src/py/ht-package/pkg pypy3 -m pip install --upgrade -r requirements.txt;)")
	endif ()
endif ()
