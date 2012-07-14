# Copyright (C) 2007-2012 Hypertable, Inc.
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

find_library(EDITLINE_LIBRARY NAMES edit PATHS
    /opt/local/lib
    /usr/local/lib
    /usr/lib
    )

find_library(NCURSES_LIBRARY NAMES ncurses PATHS
    /opt/local/lib
    /usr/local/lib
    /usr/lib
    )

if (EDITLINE_LIBRARY AND EDITLINE_INCLUDE_DIR AND NCURSES_LIBRARY)
  set(EDITLINE_LIBRARIES ${EDITLINE_LIBRARY} ${NCURSES_LIBRARY})
  set(EDITLINE_FOUND "YES")
else ()
  set(EDITLINE_FOUND "NO")
endif ()

if (EDITLINE_FOUND)
  if (NOT EDITLINE_FIND_QUIETLY)
    message(STATUS "Found Editline libraries: ${EDITLINE_LIBRARIES}")
  endif ()
else ()
  if (EDITLINE_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find suitable Editline libraries")
  endif ()
endif ()

mark_as_advanced(
  NCURSES_LIBRARY
  EDITLINE_INCLUDE_DIR
  EDITLINE_LIBRARY
  )
