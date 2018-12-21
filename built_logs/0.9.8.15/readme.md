Summary supporting the release candidate version 0.9.8.15
-
Built environment on Ubuntu 18.04(bionic) with static linking:

   -- Build was made with dev-env tree: https://github.com/kashirin-alex/environments-builder/tree/512a3b480a84dc08eff6d78716cb08baef3f47d7  
   issued with 
   	
    nohup bash ~/builder/build-env.sh --target all --sources all &> '/root/builder/built.log' &
    
   Build's cmake configuration arguments
   
    cmake `src_path` -DHT_O_LEVEL=5 -DTHRIFT_SOURCE_DIR=$BUILDS_PATH/thrift -DCMAKE_INSTALL_PREFIX=/opt/hypertable -DCMAKE_BUILD_TYPE=Release;
    
   -- cmake, make and tests log: https://github.com/kashirin-alex/hypertable/tree/0.9.8.15-rc/built_logs/0.9.8.15/built.log
	
|

    -- output# ./ht_system_info

         libHyperThirdParty-sigar-1.6.4
         {CpuInfo: vendor='Intel' model='Xeon' mhz=3900 cache_size=8192
          total_sockets=8 total_cores=8 cores_per_socket=16}
         ..
         {OsInfo: name=Linux version=4.15.0-38-generic arch=x86_64 machine=x86_64
          description='Ubuntu 18.04' patch_level=unknown
          vendor=Ubuntu vendor_version=18.04 vendor_name=Linux code_name=}
         ..
         {System: install_dir='/opt/hypertable/0.9.8.15' exe_name='ht_system_info'}
         ..

|

    /opt/hypertable/0.9.8.15/bin/ file sizes:
	
		6,244	ht_expand_hostspec
		6,616	ht_system_info
		6,800	ht_get_property
		6,800	ht_prepend_md5
		6,804	ht_prune_tsv
		7,020	htFsBrokerCeph
		7,096	htFsBrokerLocal
		7,140	htFsBrokerMapr
		7,988	ht_cluster
		8,228	ht_fsbroker
		8,236	ht_hyperspace
		8,360	ht_dumplog
		8,756	ht_ssh
		8,788	ht_log_player
		10,064	ht_gc
		10,092	ht_master
		10,096	ht_hypertable
		10,108	ht_check_integrity
		10,128	ht_rangeserver
		10,560	htHyperspace
		10,792	ht_balance_plan_generator
		11,368	ht_csdump
		11,372	ht_csvalidate
		11,404	ht_count_stored
		11,404	ht_salvage
		11,440	ht_htck
		12,180	ht_serverup
		12,264	htRangeServer
		12,340	ht_metalog_dump
		12,372	ht_metalog_tool
		12,540	htMaster
		13,372	ht_thriftbroker
		13,468	ht_load_generator
		13,776	htThriftBroker

|

    /opt/hypertable/0.9.8.15/bin/ linking 
    
        output#  ldd htRangeServer (except htFsBrokerCeph, htFsBrokerMapr, rest are the same)
	 
        linux-vdso.so.1	
        libpthread.so.0 /lib/x86_64-linux-gnu/libpthread.so.0
        libdl.so.2 /lib/x86_64-linux-gnu/libdl.so.2
        libm.so.6 /lib/x86_64-linux-gnu/libm.so.6
        libc.so.6 /lib/x86_64-linux-gnu/libc.so.6
        /lib64/ld-linux-x86-64.so.2	

|

    /opt/hypertable/0.9.8.15/lib/ file sizes:
    
		172	libHyperThirdParty.so
		296	libHyperThriftConfig.a
		760	libHyperTools.a
		1012	libHyperThirdParty.a
		2140	libHyperAppHelper.a
		4788	libHyperThrift.so
		7008	libHyperTools.so
		7056	libHyperFsBroker.so
		7084	libHyperComm.so
		7224	libHyperComm.a
		7504	libHyperCommon.so
		7540	libHyperspace.so
		7872	libHyperAppHelper.so
		9120	libHyperThriftConfig.so
		9556	libHyperMaster.so
		10512	libHyperRanger.so
		11128	libHypertable.so
		11296	libHyperCommon.a
		11704	libHyperFsBroker.a
		13288	libHyperspace.a
		19560	libHyperThrift.a
		100916	libHyperMaster.a
		101504	libHypertable.a
		190880	libHyperRanger.a

        1020    java/ht-common-0.9.8.15-bundled.jar
        9632    java/ht-thriftclient-0.9.8.15-v0.11.0-bundled.jar
        35228   java/ht-fsbroker-0.9.8.15-apache-hadoop-2.7.7-bundled.jar
        56400   java/ht-thriftclient-hadoop-tools-0.9.8.15-v0.11.0-bundled.jar

|

    /opt/hypertable/0.9.8.15/lib/ shared library linking
    
        output#  LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/opt/hypertable/0.9.8.15/lib/ \
                   ldd /opt/hypertable/0.9.8.15/lib/libHyperRanger.so
	
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

    py/ht-package 
        Python 2.7.15:
            size 2,097,216  serialized_cells.so
            (serialized_cells linked with libHyperCommon.a, libHyperThrift.a)
            
            output# ldd /usr/local/lib/python2.7/site-packages/hypertable/thrift_client/serialized_cells.so
	    
            linux-vdso.so.1 
            /lib64/ld-linux-x86-64.so.2 
            libstdc++.so.6               
            libpython2.7.so.1.0          
            libgcc_s.so.1                
            libutil.so.1                
            libpthread.so.0              
            libm.so.6                   
            libdl.so.2                  
            libc.so.6
	        
            size 4,462,528  hypertable_client.so
            
            output# ldd /usr/local/lib/python2.7/site-packages/hypertable/hypertable_client.so
			libHyperCommon.so
			libHyperComm.so
			libHypertable.so
			libHyperspace.so
			libHyperMaster.so
			libHyperRanger.so
			libHyperTools.so
			libHyperAppHelper.so
			libHyperThirdParty.so
			libHyperThrift.so
			libHyperThriftConfig.so
			libHyperFsBroker.so
			libboost_iostreams.so.1.68.0
			libboost_filesystem.so.1.68.0
			libboost_thread.so.1.68.0
			libboost_system.so.1.68.0
			libboost_chrono.so.1.68.0
			libre2.so
			libz.so.1
			libpython2.7.so.1.0
			libstdc++.so.6
			libm.so.6
			libgcc_s.so.1
			libpthread.so.0
			libc.so.6
			librt.so.1
			libbz2.so.1.0
			liblzma.so.5
			libdl.so.2
			libutil.so.1

	PyPy2 6.0:
            size 229,520  serialized_cells.so
	    
            output# ldd /opt/pypy2/site-packages/hypertable/thrift_client/serialized_cells.pypy-41.so
	    
            linux-vdso.so.1  
            /lib64/ld-linux-x86-64.so.2  
            libstdc++.so.6               
            libgcc_s.so.1
            libpthread.so.0
            libm.so.6
            libc.so.6
	        
            size 210,056  hypertable_client.so
            
            output# ldd /usr/local/lib/python2.7/site-packages/hypertable/hypertable_client.so
            
			linux-vdso.so.1
			libHyperCommon.so
			libHyperComm.so
			libHypertable.so
			libHyperspace.so
			libHyperMaster.so
			libHyperRanger.so
			libHyperTools.so
			libHyperAppHelper.so
			libHyperThirdParty.so
			libHyperThrift.so
			libHyperThriftConfig.so
			libHyperFsBroker.so
			libboost_iostreams.so.1.68.0
			libboost_filesystem.so.1.68.0
			libboost_thread.so.1.68.0
			libboost_system.so.1.68.0
			libboost_chrono.so.1.68.0
			libre2.so
			libz.so.1
			libstdc++.so.6
			libm.so.6
			libgcc_s.so.1
			libpthread.so.0
			libc.so.6
			librt.so.1
			libbz2.so.1.0
			liblzma.so.5
			/lib64/ld-linux-x86-64.so.2

 
 
  
