Summary supporting the release candidate version 0.9.8.13
-
Built environment on Ubuntu 18.04(bionic) with static linking:

   -- Build was made with dev-env tree 10a9ef6: https://github.com/kashirin-alex/environments-builder/tree/10a9ef6  
   issued with 
   	
    nohup bash ~/builder/build-env.sh --target all --sources all &> '/root/builder/built.log' &
    
   Build's cmake configuration arguments
   
    cmake `src_path` -DHT_O_LEVEL=5 -DTHRIFT_SOURCE_DIR=$BUILDS_PATH/thrift -DCMAKE_INSTALL_PREFIX=/opt/hypertable -DCMAKE_BUILD_TYPE=Release;
    
   -- cmake, make and tests log: https://github.com/kashirin-alex/hypertable/tree/0.9.8.13-rc/built_logs/0.9.8.13/built.log
	
|

    -- output# ./ht_system_info

         libHyperThirdParty-sigar-1.6.4
         {CpuInfo: vendor='Intel' model='Core(TM) i7-4770 CPU @ 3.40GHz' mhz=3900 cache_size=8192 
	  total_sockets=8 total_cores=8 cores_per_socket=16}
         ..
         {OsInfo: name=Linux version=4.15.0-20-generic arch=x86_64 machine=x86_64
          description='Ubuntu 18.04' patch_level=unknown
          vendor=Ubuntu vendor_version=18.04 vendor_name=Linux code_name=}
         ..
         {System: install_dir='/opt/hypertable/0.9.8.13' exe_name='ht_system_info'}
         ..

|

    /opt/hypertable/0.9.8.13/bin/ file sizes:
 
         4,611,192 ht_expand_hostspec
         4,995,024 ht_system_info
         5,237,744 ht_prepend_md5
         5,242,680 ht_get_property
         5,246,512 ht_prune_tsv
         5,644,456 htFsBrokerMapr
         5,652,592 htFsBrokerLocal
         5,977,264 ht_cluster
         6,324,000 ht_fsbroker
         6,411,184 ht_hyperspace
         6,462,896 ht_dumplog
         7,108,288 ht_log_player
         7,337,296 ht_ssh
         9,056,256 ht_gc
         9,086,496 ht_master
         9,089,184 ht_hypertable
         9,100,000 ht_check_integrity
         9,138,336 ht_rangeserver
         9,245,592 htHyperspace
         9,951,168 ht_balance_plan_generator
        10,665,432 ht_csdump
        10,670,304 ht_csvalidate
        10,706,712 ht_count_stored
        10,722,112 ht_salvage
        10,760,544 ht_htck
        11,833,136 htRangeServer
        11,949,208 ht_serverup
        11,950,072 ht_metalog_dump
        11,974,656 ht_metalog_tool
        12,232,656 htMaster
        13,854,208 ht_thriftbroker
        13,980,928 ht_load_generator
        14,335,040 htThriftBroker

|

    /opt/hypertable/0.9.8.13/bin/ linking 
    
        output#  ldd htRangeServer (all executables are the same)
        -- to note libz, liblzma and libbz2 are linked by libboost_iostreams
	 
         libz.so.1                          /usr/local/lib/libz.so.1
         liblzma.so.5                       /usr/local/lib/liblzma.so.5
         libbz2.so.1.0                      /usr/local/lib/libbz2.so.1.0
         libstdc++.so.6                     /usr/local/lib/libstdc++.so.6
         libgcc_s.so.1                      /usr/local/lib/libgcc_s.so.1
         libboost_thread.so.1.66.0          /usr/local/lib/libboost_thread.so.1.66.0
         libboost_system.so.1.66.0          /usr/local/lib/libboost_system.so.1.66.0
         libboost_program_options.so.1.66.0 /usr/local/lib/libboost_program_options.so.1.66.0
         libboost_iostreams.so.1.66.0       /usr/local/lib/libboost_iostreams.so.1.66.0
         libboost_filesystem.so.1.66.0      /usr/local/lib/libboost_filesystem.so.1.66.0
         libboost_chrono.so.1.66.0          /usr/local/lib/libboost_chrono.so.1.66.0
         librt.so.1                         /lib/x86_64-linux-gnu/librt.so.1
         libpthread.so.0                    /lib/x86_64-linux-gnu/libpthread.so.0
         libm.so.6                          /lib/x86_64-linux-gnu/libm.so.6
         libdl.so.2                         /lib/x86_64-linux-gnu/libdl.so.2
         libc.so.6                          /lib/x86_64-linux-gnu/libc.so.6
         linux-vdso.so.1 
         /lib64/ld-linux-x86-64.so.2 

|

    /opt/hypertable/0.9.8.13/lib/ file sizes:
    
             45,720 libboost_chrono.so.1.66.0 
            135,528 libboost_filesystem.so.1.66.0 
            166,280 libboost_iostreams.so.1.66.0 
            675,520 libboost_program_options.so.1.66.0 
             25,080 libboost_system.so.1.66.0 
            205,064 libboost_thread.so.1.66.0 
            751,240 libgcc_s.so.1 
            229,193 libhdfs.so.0.0.0 
          2,387,904 libHyperAppHelper.a 
          5,687,472 libHyperAppHelper.so 
          7,394,534 libHyperComm.a 
          5,401,816 libHyperComm.so 
         10,653,270 libHyperCommon.a 
          5,731,840 libHyperCommon.so 
         12,329,060 libHyperFsBroker.a 
          5,393,640 libHyperFsBroker.so 
        107,754,772 libHyperMaster.a 
          7,358,008 libHyperMaster.so 
        200,998,404 libHyperRanger.a 
          8,306,208 libHyperRanger.so 
         14,335,378 libHyperspace.a 
          5,890,208 libHyperspace.so 
        107,624,422 libHypertable.a 
          8,916,720 libHypertable.so 
          1,030,640 libHyperThirdParty.a 
            178,600 libHyperThirdParty.so 
         19,484,886 libHyperThrift.a 
          5,132,592 libHyperThrift.so 
            744,808 libHyperThriftConfig.a 
          7,448,896 libHyperThriftConfig.so 
            934,124 libHyperTools.a 
          5,337,816 libHyperTools.so 
            246,605 libsigar-amd64-linux.so 
         12,178,800 libstdc++.so.6 
          1,041,152 java/ht-common-0.9.8.13-bundled.jar
         36,190,682 java/ht-fsbroker-0.9.8.13-apache-hadoop-2.7.6-bundled.jar
          9,856,562 java/ht-thriftclient-0.9.8.13-v0.10.0-bundled.jar
         57,750,326 java/ht-thriftclient-hadoop-tools-0.9.8.13-v0.10.0-bundled.jar

|

    /opt/hypertable/0.9.8.13/lib/ shared library linking
    
        output#  LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/opt/hypertable/0.9.8.13/lib/ ldd /opt/hypertable/0.9.8.13/lib/libHyperRanger.so
	
        libHyperComm.so        /opt/hypertable/0.9.8.13/lib/libHyperComm.so
        libHyperCommon.so      /opt/hypertable/0.9.8.13/lib/libHyperCommon.so
        libHyperFsBroker.so    /opt/hypertable/0.9.8.13/lib/libHyperFsBroker.so
        libHyperspace.so       /opt/hypertable/0.9.8.13/lib/libHyperspace.so
        libHypertable.so       /opt/hypertable/0.9.8.13/lib/libHypertable.so
        libHyperThirdParty.so  /opt/hypertable/0.9.8.13/lib/libHyperThirdParty.so
        libHyperTools.so       /opt/hypertable/0.9.8.13/lib/libHyperTools.so
        libboost_chrono.so.1.66.0          /opt/hypertable/0.9.8.13/lib/libboost_chrono.so.1.66.0
        libboost_filesystem.so.1.66.0      /opt/hypertable/0.9.8.13/lib/libboost_filesystem.so.1.66.0
        libboost_iostreams.so.1.66.0       /opt/hypertable/0.9.8.13/lib/libboost_iostreams.so.1.66.0
        libboost_program_options.so.1.66.0 /opt/hypertable/0.9.8.13/lib/libboost_program_options.so.1.66.0
        libboost_system.so.1.66.0          /opt/hypertable/0.9.8.13/lib/libboost_system.so.1.66.0
        libboost_thread.so.1.66.0          /opt/hypertable/0.9.8.13/lib/libboost_thread.so.1.66.0
        libstdc++.so.6         /opt/hypertable/0.9.8.13/lib/libstdc++.so.6
        libgcc_s.so.1          /opt/hypertable/0.9.8.13/lib/libgcc_s.so.1
        liblzma.so.5    /usr/local/lib/liblzma.so.5
        libz.so.1       /usr/local/lib/libz.so.1
        libbz2.so.1.0   /usr/local/lib/libbz2.so.1.0
        libm.so.6       /lib/x86_64-linux-gnu/libm.so.6
        libpthread.so.0 /lib/x86_64-linux-gnu/libpthread.so.0
        librt.so.1      /lib/x86_64-linux-gnu/librt.so.1
        libc.so.6       /lib/x86_64-linux-gnu/libc.so.6
        libdl.so.2      /lib/x86_64-linux-gnu/libdl.so.2
        /lib64/ld-linux-x86-64.so.2 
        linux-vdso.so.1 

|

    py/ht-package (serialized_cells linked with libHyperCommon.a, libHyperThrift.a)
        Python 2.7.15:
            size 217,376  serialized_cells.so
	    
            output# ldd /usr/local/lib/python2.7/site-packages/hypertable/thrift_client/serialized_cells.so
	    
            linux-vdso.so.1 
            /lib64/ld-linux-x86-64.so.2 
            libstdc++.so.6               /usr/local/lib/libstdc++.so.6
            libpython2.7.so.1.0          /usr/local/lib/libpython2.7.so.1.0
            libgcc_s.so.1                /usr/local/lib/libgcc_s.so.1
            libutil.so.1                 /lib/x86_64-linux-gnu/libutil.so.1
            libpthread.so.0              /lib/x86_64-linux-gnu/libpthread.so.0
            libm.so.6                    /lib/x86_64-linux-gnu/libm.so.6
            libdl.so.2                   /lib/x86_64-linux-gnu/libdl.so.2
            libc.so.6                    /lib/x86_64-linux-gnu/libc.so.6
	
	PyPy2 6.0:
            size 201,368  serialized_cells.so
	    
            output# ldd /opt/pypy2/site-packages/hypertable/thrift_client/serialized_cells.pypy-41.so
	    
            linux-vdso.so.1  
            /lib64/ld-linux-x86-64.so.2  
            libstdc++.so.6               /usr/local/lib/libstdc++.so.6 
            libgcc_s.so.1                /usr/local/lib/libgcc_s.so.1 
            libpthread.so.0              /lib/x86_64-linux-gnu/libpthread.so.0 
            libm.so.6                    /lib/x86_64-linux-gnu/libm.so.6 
            libc.so.6                    /lib/x86_64-linux-gnu/libc.so.6 

 
 
  
