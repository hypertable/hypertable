# CMake file for contrib code

if (JAVA_INCLUDE_PATH AND HADOOP_LIB_PATH AND HADOOP_INCLUDE_PATH
    AND BUILD_SHARED_LIBS)
  set(BUILD_MAPREDUCE ON)
endif ()

#if (BUILD_MAPREDUCE)
#  add_subdirectory(cc/MapReduce)
#endif ()
#add_subdirectory(cc/PythonBinding)
