# CMake file for hypertable source

add_subdirectory(cc/Common)
add_subdirectory(cc/HyperAppHelper)
add_subdirectory(cc/ThirdParty)
add_subdirectory(cc/AsyncComm)
add_subdirectory(cc/Hyperspace)
add_subdirectory(cc/Hypertable)
add_subdirectory(cc/Tools)
add_subdirectory(cc/FsBroker/Lib)
add_subdirectory(cc/FsBroker/local)

add_subdirectory(py/ganglia)
add_subdirectory(rb/Monitoring)


# THRIFT CLIENTS
if (THRIFT_FOUND)
  add_subdirectory(cc/ThriftBroker)
  
  if (LANGS OR LANG_JAVA)
    add_subdirectory(java/ThriftClient)
  endif ()
  if (LANGS OR LANG_JS)
    add_subdirectory(js)
  endif ()
  if (LANGS OR LANG_RB)
    add_subdirectory(rb/ThriftClient)
  endif ()
  if (LANGS OR LANG_PL)
	add_subdirectory(perl/ThriftClient)
  endif ()
  if (LANGS OR LANG_PHP)
   add_subdirectory(php/ThriftClient)
  endif ()
endif ()


# DISTRIBUTES FILE SYSTEMS
if (CEPH_FOUND)
   add_subdirectory(cc/FsBroker/ceph)
endif ()

if (MAPR_FOUND)
   add_subdirectory(cc/FsBroker/mapr)
endif ()

if (Qfs_FOUND)
   add_subdirectory(cc/FsBroker/qfs)
endif ()


if (LANGS OR LANG_PY2 OR LANG_PY3 OR LANG_PYPY2 OR LANG_PYPY3)
  add_subdirectory(py/ht-package)
endif ()

if (LANGS OR LANG_JAVA OR FSBROKERS OR FSBROKER_HDFS)
	if(NOT SKIP_JAVA_BUILD)
		add_subdirectory(java)
	endif ()
endif ()




#if (LANGS OR LANG_PY2 OR LANG_PY3 OR LANG_PYPY2 OR LANG_PYPY3)
#  if (PYTHONTHRIFT_FOUND AND (PYTHON2_FOUND OR PYTHON3_FOUND OR PYPY2_FOUND OR PYPY3_FOUND))
#    # add_subdirectory(cc/HyperPython)
#    #add_subdirectory(py/libpyhypertable)
#  endif ()
#endif ()
