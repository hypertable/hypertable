Summary supporting the release candidate version 0.9.8.14
-
Built environment on Ubuntu 18.04(bionic) with static linking:

   -- Build was made with dev-env tree 10a9ef6: https://github.com/kashirin-alex/environments-builder/tree/ecc1b16  
   issued with 
   	
    nohup bash ~/builder/build-env.sh --target all --sources all &> '/root/builder/built.log' &
    
   Build's cmake configuration arguments
   
    cmake `src_path` -DHT_O_LEVEL=5 -DTHRIFT_SOURCE_DIR=$BUILDS_PATH/thrift -DCMAKE_INSTALL_PREFIX=/opt/hypertable -DCMAKE_BUILD_TYPE=Release;
    
   -- cmake, make and tests log: https://github.com/kashirin-alex/hypertable/tree/0.9.8.14-rc/built_logs/0.9.8.14/built.log
	
|

    -- output# ./ht_system_info

         libHyperThirdParty-sigar-1.6.4
         {CpuInfo: vendor='Intel' model='Xeon' mhz=3900 cache_size=8192
          total_sockets=8 total_cores=8 cores_per_socket=16}
         ..
         {OsInfo: name=Linux version=4.15.0-29-generic arch=x86_64 machine=x86_64
          description='Ubuntu 18.04' patch_level=unknown
          vendor=Ubuntu vendor_version=18.04 vendor_name=Linux code_name=}
         ..
         {System: install_dir='/opt/hypertable/0.9.8.14' exe_name='ht_system_info'}
         ..

|

    /opt/hypertable/0.9.8.14/bin/ file sizes:
	
         6,855,456  ht_expand_hostspec
         7,114,336  ht_system_info
         7,298,752  ht_prepend_md5
         7,302,848  ht_get_property
         7,302,848  ht_prune_tsv
         7,540,592  htFsBrokerCeph
         7,610,096  htFsBrokerLocal
         7,995,584  ht_cluster
         8,237,968  ht_fsbroker
         8,250,928  ht_hyperspace
         8,376,624  ht_dumplog
         8,812,176  ht_log_player
         9,291,488  ht_ssh
         10,122,808 ht_gc
         10,147,416 ht_hypertable
         10,147,416 ht_master
         10,159,672 ht_check_integrity
         10,180,184 ht_rangeserver
         10,860,760 ht_balance_plan_generator
         11,138,128 htHyperspace
         11,433,624 ht_csdump
         11,437,688 ht_csvalidate
         11,466,424 ht_count_stored
         11,474,584 ht_salvage
         11,515,512 ht_htck
         12,282,616 ht_serverup
         12,334,840 htRangeServer
         12,429,656 ht_metalog_dump
         12,458,328 ht_metalog_tool
         12,618,072 htMaster
         13,503,352 ht_thriftbroker
         13,597,400 ht_load_generator
         13,904,536 htThriftBroker

|

    /opt/hypertable/0.9.8.14/bin/ linking 
    
        output#  ldd htRangeServer (except htFsBrokerCeph, all executables are the same)
	 
         linux-vdso.so.1	
         libpthread.so.0 /lib/x86_64-linux-gnu/libpthread.so.0
         libdl.so.2 /lib/x86_64-linux-gnu/libdl.so.2
         libm.so.6 /lib/x86_64-linux-gnu/libm.so.6
         libc.so.6 /lib/x86_64-linux-gnu/libc.so.6
         /lib64/ld-linux-x86-64.so.2	

|

    /opt/hypertable/0.9.8.14/lib/ file sizes:
    
             174,640 libHyperThirdParty.so
             246,605 libsigar-amd64-linux.so
             752,278 libHyperThriftConfig.a
             870,720 libgcc_s.so.1
             951,168 libHyperTools.a
           1,033,776 libHyperThirdParty.a
           1,545,136 libcephfs.so.2
           2,334,900 libHyperAppHelper.a
           4,899,240 libHyperThrift.so
           7,562,808 libHyperComm.a
           7,814,608 libHyperTools.so
           7,880,648 libHyperFsBroker.so
           7,889,840 libHyperComm.so
           8,151,216 libHyperAppHelper.so
           8,202,608 libHyperCommon.so
           8,372,848 libHyperspace.so
           9,452,184 libHyperThriftConfig.so
           9,880,752 libHyperMaster.so
          10,831,640 libHyperRanger.so
          10,923,934 libHyperCommon.a
          11,482,928 libHypertable.so
          12,447,688 libHyperFsBroker.a
          14,851,222 libHyperspace.a
          15,174,312 libstdc++.so.6
          20,027,394 libHyperThrift.a
         110,262,654 libHypertable.a
         110,459,334 libHyperMaster.a
         205,886,196 libHyperRanger.a
           1,041,152 java/ht-common-0.9.8.14-bundled.jar
           9,862,730 java/ht-thriftclient-0.9.8.14-v0.11.0-bundled.jar
          36,071,599 java/ht-fsbroker-0.9.8.14-apache-hadoop-2.7.7-bundled.jar
          57,750,329 java/ht-thriftclient-hadoop-tools-0.9.8.14-v0.11.0-bundled.jar

|

    /opt/hypertable/0.9.8.14/lib/ shared library linking
    
        output#  LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/opt/hypertable/0.9.8.14/lib/ ldd /opt/hypertable/0.9.8.14/lib/libHyperRanger.so
	
         libc.so.6               /lib/x86_64-linux-gnu/libc.so.6
         libdl.so.2              /lib/x86_64-linux-gnu/libdl.so.2
         libm.so.6               /lib/x86_64-linux-gnu/libm.so.6
         libpthread.so.0         /lib/x86_64-linux-gnu/libpthread.so.0
         libgcc_s.so.1           /opt/hypertable/0.9.8.14/lib/libgcc_s.so.1
         libHyperComm.so         /opt/hypertable/0.9.8.14/lib/libHyperComm.so
         libHyperCommon.so       /opt/hypertable/0.9.8.14/lib/libHyperCommon.so
         libHyperFsBroker.so     /opt/hypertable/0.9.8.14/lib/libHyperFsBroker.so
         libHyperspace.so        /opt/hypertable/0.9.8.14/lib/libHyperspace.so
         libHypertable.so        /opt/hypertable/0.9.8.14/lib/libHypertable.so
         libHyperThirdParty.so   /opt/hypertable/0.9.8.14/lib/libHyperThirdParty.so
         libHyperTools.so        /opt/hypertable/0.9.8.14/lib/libHyperTools.so
         libstdc++.so.6          /opt/hypertable/0.9.8.14/lib/libstdc++.so.6
         linux-vdso.so.1 
         /lib64/ld-linux-x86-64.so.2 

|

    py/ht-package (serialized_cells linked with libHyperCommon.a, libHyperThrift.a)
        Python 2.7.15:
            size 2,095,128  serialized_cells.so
	    
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
            size 225,152  serialized_cells.so
	    
            output# ldd /opt/pypy2/site-packages/hypertable/thrift_client/serialized_cells.pypy-41.so
	    
            linux-vdso.so.1  
            /lib64/ld-linux-x86-64.so.2  
            libstdc++.so.6               /usr/local/lib/libstdc++.so.6 
            libgcc_s.so.1                /usr/local/lib/libgcc_s.so.1 
            libpthread.so.0              /lib/x86_64-linux-gnu/libpthread.so.0 
            libm.so.6                    /lib/x86_64-linux-gnu/libm.so.6 
            libc.so.6                    /lib/x86_64-linux-gnu/libc.so.6 

 
 
  
