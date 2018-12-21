THIS BRANCH OF HYPERTABLE
==============
This branch is a fork of hypertable/0.9.8.11 and maintained for purpose to work with an up to date components and further developments with intrest to pull to the upstream.

HOW TO INSTALL
==============

Binary install release is not available at this branch and you can use the releases from hypertable upstream [https://github.com/hypertable/hypertable]


HOW TO BUILD FROM SOURCE
========================

A build can be done using the  [https://github.com/kashirin-alex/environments-builder]
 
Configuration options with cmake:
  
  Support for libraries in the different languages can be set with the LANGS param by comman delimited languages(ext+version)
    (default is "all", for None, "none" should be used) 
  
        -Dlanguages=php,pl,py2,pypy2,py3,pypy3,java,js,rb   

  File System Brokers can be set with the FSBROKERS param by comman delimited of hdfs,qfs,ceph,mapr  
  
        -Dfsbrokers=hdfs,ceph,qfs,mapr    
  
  Followed with optionally another param 
        (default is the available setup of hadoop)
        
        -Dhdfs_vers=apache-2.7.5,apache-1.1.0,apache-1.1.1,apache-1.2.1
        
  Compilation Optimizations, cmake arguments HT_ENABLE_SHARED, BUILD_WITH_STATIC, HT_TEST_WITH and HT_O_LEVEL(predefined arguments combinations) with HT_O_LEVEL=4 compiler flags -O3 with LTO support are added,  default optimization level is 3:
        
        -DHT_O_LEVEL=[1-6]
        
   1: Optimals for debugging (fast)                 
        (HT_ENABLE_SHARED=ON HT_TEST_WITH=SHARED)
        
   2: Optimals for Integrations and Storage space   
        (BUILD_SHARED_LIBS=ON BUILD_WITH_STATIC=OFF)
        
   3: Optimals for Integrations and Storage space   
        (HT_ENABLE_SHARED=ON BUILD_WITH_STATIC=OFF HT_TEST_WITH=SHARED)
        
   4: Optimals for Integrations and Performance   
        (HT_ENABLE_SHARED=ON BUILD_WITH_STATIC=OFF HT_TEST_WITH=SHARED +Flags -O3 LTO)
        
   5: Optimals for Integrations, Performance and Memory with testing   
        (HT_ENABLE_SHARED=ON BUILD_WITH_STATIC=ON HT_TEST_WITH=BOTH +Flags -O3 LTO)
        
   6: Optimals for Integrations, Performance and Memory without testing   
        (HT_ENABLE_SHARED=ON BUILD_WITH_STATIC=ON HT_TEST_WITH=NONE +Flags -O3 LTO)
       
   -- result of flags from level=4, -flto -fuse-linker-plugin -ffat-lto-objects -O3, the static libraries file size of libHyperRanger.a for example is	200,920,572 bytes


        

To configuration options of "languages" and "fsbrokers", follow apply:
   - default is "all" and "none" should be used for None,
   - if set and the depenencies are not meet it will quit wit fatal error
     
for more guidence you can follow with the Hypertable repository [https://github.com/hypertable/hypertable]
