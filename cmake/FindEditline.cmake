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

# Find the Editline includes and libraries
# This module defines
# EDITLINE_INCLUDE_DIR, where to find editline/readline.h, etc.
# EDITLINE_LIBRARIES, the libraries needed to use editline.
# EDITLINE_FOUND, If false, do not try to use editline.
# also defined, but not for general use are
# EDITLINE_LIBRARY, NCURSES_LIBRARY

find_path(EDITLINE_INCLUDE_DIR editline/readline.h
    /opt/local/include
    /usr/local/include
    /usr/include
    )

find_library(EDITLINE_LIBRARY NAMES libedit.a edit PATHS
    /opt/local/lib
    /usr/local/lib
    /usr/lib
    )

if (EDITLINE_LIBRARY AND EDITLINE_INCLUDE_DIR)  # IF EDITLINE BUILT WITH NCURSESW LINKED
  set(EDITLINE_LIBRARIES ${EDITLINE_LIBRARY})
  set(EDITLINE_FOUND "YES")
else ()
  message(FATAL_ERROR "Could not find suitable Editline libraries")
  set(EDITLINE_FOUND "NO")
endif ()


find_library(NCURSESW_LIBRARY NAMES ncursesw PATHS
	/opt/local/lib
	/usr/local/lib
	/usr/lib
	)
if (NCURSESW_LIBRARY)
	set(NCURSES_LIBRARY ${NCURSESW_LIBRARY})
else ()
	find_library(NCURSES_LIBRARY NAMES ncurses PATHS
        /opt/local/lib
        /usr/local/lib
        /usr/lib
		)
endif ()


if (NCURSES_LIBRARY)
    set(EDITLINE_LIBRARIES ${EDITLINE_LIBRARY} ${NCURSES_LIBRARY})
    set(EDITLINE_FOUND "YES")
    message(STATUS "Found Editline with Ncurses: ${EDITLINE_LIBRARIES}")

else ()
    message(STATUS "Found Editline: ${EDITLINE_LIBRARY}")
    CHECK_SYMBOL_EXISTS('setupterm' ${EDITLINE_INCLUDE_DIR} EDITLINE_CHECK_BUILD)
    if (NOT EDITLINE_CHECK_BUILD)
		set(EDITLINE_FOUND "NO")
        message(FATAL_ERROR "Could not find suitable Editline libraries")
	endif ()
	set(NCURSES_LIBRARY ${EDITLINE_LIBRARY})
endif ()

if (EDITLINE_FOUND)
	try_run(EDITLINE_CHECK EDITLINE_CHECK_BUILD
                ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
                ${HYPERTABLE_SOURCE_DIR}/cmake/CheckEditline.cc
                CMAKE_FLAGS -DINCLUDE_DIRECTORIES=${EDITLINE_INCLUDE_DIR}
                LINK_LIBRARIES ${EDITLINE_LIBRARIES}
                OUTPUT_VARIABLE EDITLINE_TRY_OUT)
    if (EDITLINE_CHECK_BUILD STREQUAL "FALSE")
         message(STATUS "${EDITLINE_TRY_OUT}")
         message(FATAL_ERROR "Please fix the Editline installation and try again.  Make sure you build libedit with --enable-widec!")
         set(EDITLINE_LIBRARIES)
    endif ()
	
	HT_INSTALL_LIBS(lib ${EDITLINE_LIBRARIES})
endif ()

if (NCURSES_LIBRARY)
	mark_as_advanced(
        NCURSES_LIBRARY
        EDITLINE_INCLUDE_DIR
        EDITLINE_LIBRARY
    )
else ()
	mark_as_advanced(
		EDITLINE_INCLUDE_DIR
		EDITLINE_LIBRARY
    )
endif ()
