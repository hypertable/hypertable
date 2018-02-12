# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

# - Find PYTHON - on Major version
#  PYTHON_INCLUDE_DIR - where to find Python.h
#  PYTHON_LIBRARIES   - List of libraries when using python-devel
#  PYTHON_FOUND       - True if python-devel was found

# PYTHON 2
if (LANGS OR LANG_PY2)
	exec_program(env ARGS python -V OUTPUT_VARIABLE PYTHON_VERSION_STRING
				RETURN_VALUE PYTHON_RETURN)
	if (PYTHON_RETURN  STREQUAL "0")
		STRING(REGEX REPLACE ".*Python ([0-9]+.[0-9]+).*" "\\1" PYTHON_VERSION "${PYTHON_VERSION_STRING}")

		find_path(PYTHON2_INCLUDE_DIR Python.h NO_DEFAULT_PATH PATHS
            ${HT_DEPENDENCY_INCLUDE_DIR}/python${PYTHON_VERSION}
            /opt/local/include/python${PYTHON_VERSION}
            /usr/local/include/python${PYTHON_VERSION}
            /usr/include/python${PYTHON_VERSION}
            )
		find_library(PYTHON2_LIBRARY python${PYTHON_VERSION} NO_DEFAULT_PATH PATHS
               ${HT_DEPENDENCY_LIB_DIR}
               /opt/local/lib
               /usr/local/lib
               /usr/lib
               /usr/lib/x86_64-linux-gnu
               )
	elseif (LANG_PY2)
	    message(FATAL_ERROR "Requested for language, python2 is not available")
	endif ()
	
	if (PYTHON2_INCLUDE_DIR AND PYTHON2_LIBRARY)
		set(PYTHON2_FOUND TRUE)
		message(STATUS "Found Python${PYTHON_VERSION}-devel: ${PYTHON2_LIBRARY}")
		set(PYTHON2_LIBRARY ${PYTHON2_LIBRARY} ${BOOST_PYTHON_LIB})
	else ()
		set(PYTHON2_FOUND FALSE)
	endif ()
endif ()
  

# PYTHON 3
if (LANGS OR LANG_PY3)
	exec_program(env ARGS python3 -V OUTPUT_VARIABLE PYTHON_VERSION_STRING
				RETURN_VALUE PYTHON_RETURN)
	
	if (PYTHON_RETURN  STREQUAL "0")
	
		STRING(REGEX REPLACE ".*Python ([0-9]+.[0-9]+).*" "\\1" PYTHON_VERSION "${PYTHON_VERSION_STRING}")

		find_path(PYTHON3_INCLUDE_DIR Python.h NO_DEFAULT_PATH PATHS
            ${HT_DEPENDENCY_INCLUDE_DIR}/python${PYTHON_VERSION}
            /opt/local/include/python${PYTHON_VERSION}
            /usr/local/include/python${PYTHON_VERSION}
            /usr/include/python${PYTHON_VERSION}
            ${HT_DEPENDENCY_INCLUDE_DIR}/python${PYTHON_VERSION}m
            /opt/local/include/python${PYTHON_VERSION}m
            /usr/local/include/python${PYTHON_VERSION}m
            /usr/include/python${PYTHON_VERSION}m
            )
		find_library(PYTHON3_LIBRARY python${PYTHON_VERSION} NO_DEFAULT_PATH PATHS
               ${HT_DEPENDENCY_LIB_DIR}
               /opt/local/lib
               /usr/local/lib
               /usr/lib
               /usr/lib/x86_64-linux-gnu
               )
		if(NOT PYTHON3_LIBRARY)
			find_library(PYTHON3_LIBRARY python${PYTHON_VERSION}m NO_DEFAULT_PATH PATHS
				${HT_DEPENDENCY_LIB_DIR}
				/opt/local/lib
				/usr/local/lib
				/usr/lib
				/usr/lib/x86_64-linux-gnu
               )
		endif ()
	elseif (LANG_PY3)
	    message(FATAL_ERROR "Requested for language, python3 is not available")
	endif ()
	if (PYTHON3_INCLUDE_DIR AND PYTHON3_LIBRARY)
		set(PYTHON3_FOUND TRUE)
		message(STATUS "Found Python${PYTHON_VERSION}-devel: ${PYTHON3_LIBRARY}")
		set(PYTHON3_LIBRARY ${PYTHON3_LIBRARY} ${BOOST_PYTHON_LIB})
	else ()
		set(PYTHON3_FOUND FALSE)
  endif ()
endif ()



# PYPY 2
if (LANGS OR LANG_PYPY2)
	execute_process(COMMAND pypy -c "from distutils import sysconfig as s;import sys;
print(s.get_python_inc(plat_specific=True));
print(sys.prefix);
"
    RESULT_VARIABLE _PYPY_SUCCESS
    OUTPUT_VARIABLE _PYPY_VALUES
    )
	if (_PYPY_SUCCESS  STREQUAL "0")
		string(REGEX REPLACE ";" "\\\\;" _PYPY_VALUES ${_PYPY_VALUES})
		string(REGEX REPLACE "\n" ";" _PYPY_VALUES ${_PYPY_VALUES})
		list(GET _PYPY_VALUES 0 PYPY2_INCLUDE_DIR)
		list(GET _PYPY_VALUES 1 PYPY2_LIBDIR)
		set(PYPY2_LIBDIR ${PYPY2_LIBDIR}/bin/libpypy-c.so)
	elseif(LANG_PYPY2)
	    message(FATAL_ERROR "Requested for language, pypy2 is not available")
	endif ()
	if (PYPY2_INCLUDE_DIR AND PYPY2_LIBDIR)
		set(PYPY2_FOUND TRUE)
		message(STATUS "Found PyPy2-devel: ${PYPY2_LIBDIR}")
	else ()
		set(PYPY2_FOUND FALSE)
	endif ()
endif ()

# PYPY 3
if (LANGS OR LANG_PYPY3)
	execute_process(COMMAND pypy3 -c "from distutils import sysconfig as s;import sys;
print(s.get_python_inc(plat_specific=True));
print(sys.prefix);
"
    RESULT_VARIABLE _PYPY_SUCCESS
    OUTPUT_VARIABLE _PYPY_VALUES
    )
	if (_PYPY_SUCCESS  STREQUAL "0")
		string(REGEX REPLACE ";" "\\\\;" _PYPY_VALUES ${_PYPY_VALUES})
		string(REGEX REPLACE "\n" ";" _PYPY_VALUES ${_PYPY_VALUES})
		list(GET _PYPY_VALUES 0 PYPY3_INCLUDE_DIR)
		list(GET _PYPY_VALUES 1 PYPY3_LIBDIR)
		set(PYPY3_LIBDIR ${PYPY3_LIBDIR}/bin/libpypy-c.so)
	elseif (LANG_PYPY3)
	    message(FATAL_ERROR "Requested for language, pypy3 is not available")
	endif ()
	if (PYPY3_INCLUDE_DIR AND PYPY3_LIBDIR)
		set(PYPY3_FOUND TRUE)
		message(STATUS "Found PyPy3-devel: ${PYPY3_LIBDIR}")
	else ()
		set(PYPY3_FOUND FALSE)
	endif ()
endif ()

if (PYPY2_FOUND OR PYPY3_FOUND)
	find_path(Pybind11_INCLUDE_DIR pybind11/pybind11.h PATHS
		/usr/local/include
		/opt/local/include
	)
	if (Pybind11_INCLUDE_DIR)
		message(STATUS "Found Pybind11: ${Pybind11_INCLUDE_DIR}")
		set(Pybind11_FOUND TRUE)
	else ()
		if(LANG_PYPY2 OR LANG_PYPY3) 
			    message(FATAL_ERROR "Requested for language pypy, dependency Pybind11 is not available")
		endif ()
		set(Pybind11_FOUND FALSE)
	endif ()
endif ()
