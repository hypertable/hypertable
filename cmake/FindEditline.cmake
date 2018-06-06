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


HT_FASTLIB_SET(
	NAME "EDITLINE" 
	REQUIRED TRUE 
	LIB_PATHS 
	INC_PATHS 
	STATIC libedit.a 
	SHARED edit
	INCLUDE editline/readline.h
)

HT_FASTLIB_SET(
	NAME "NCURSESW" 
	REQUIRED TRUE 
	LIB_PATHS 
	INC_PATHS 
	STATIC libncursesw.a 
	SHARED ncursesw
	INCLUDE ncurses.h
)
if (NOT NCURSESW_LIBRARIES)
	HT_FASTLIB_SET(
		NAME "NCURSESW" 
		REQUIRED TRUE 
		LIB_PATHS 
		INC_PATHS 
		STATIC libncurses.a 
		SHARED ncurses
		INCLUDE ncurses.h
	)
endif ()
HT_FASTLIB_SET(
	NAME "TINFOW" 
	REQUIRED FALSE 
	LIB_PATHS 
	INC_PATHS 
	STATIC libtinfow.a 
	SHARED tinfow
	INCLUDE termcap.h
)
if(NOT TINFOW_LIBRARIES)
	set(TINFOW_LIBRARIES ${NCURSESW_LIBRARIES})
endif ()

try_run(EDITLINE_CHECK EDITLINE_CHECK_BUILD
        ${HYPERTABLE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeTmp
        ${HYPERTABLE_SOURCE_DIR}/cmake/CheckEditline.cc
        CMAKE_FLAGS 
        INCLUDE_DIRECTORIES ${EDITLINE_INCLUDE_DIR}
        LINK_LIBRARIES ${EDITLINE_LIBRARIES} ${NCURSESW_LIBRARIES} ${TINFOW_LIBRARIES} ${BDB_LIBRARIES} pthread
        OUTPUT_VARIABLE EDITLINE_TRY_OUT)
if (EDITLINE_CHECK_BUILD STREQUAL "FALSE")
	message(STATUS "${EDITLINE_TRY_OUT}")
	message(FATAL_ERROR "Please fix the Editline installation and try again.  Make sure you build libedit with --enable-widec!")
endif ()
