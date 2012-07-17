#!/usr/bin/env bash

arch=`uname -m`

if [ $arch == "i386" ] || [ $arch == "i586" ] || [ $arch == "i686" ] ; then
  ARCH=32
elif [ $arch == "x86_64" ] ; then
  ARCH=64
else
  echo "Unknown processor architecture: $arch"
  exit 1
fi

# intall keys for ceph release
wget -q -O- https://raw.github.com/ceph/ceph/master/keys/autobuild.asc \ | sudo apt-key add -

# add the ceph repo 
echo deb http://gitbuilder.ceph.com/ceph-deb-$(lsb_release -sc)-$($arch)-basic/ref/master $(lsb_release -sc) main | sudo tee /etc/apt/sources.list.d/ceph.list


apt-get -y update
apt-get -y --allow-unauthenticated install zip g++ cmake liblog4cpp5-dev libbz2-dev git-core cronolog zlib1g-dev libexpat1-dev libncurses-dev libreadline5-dev rrdtool librrd2-dev libart-2.0-2 libart-2.0-dev

# no install ceph and the new lib and dev headers - posibly dont need ceph just to build but is needed for testing
apt-get -y install ceph libcephfs

# Boost
cd ~
wget http://downloads.sourceforge.net/boost/boost_1_44_0.tar.bz2
tar xjvf boost_1_44_0.tar.bz2
cd boost_1_44_0
./bootstrap.sh --with-libraries=filesystem,iostreams,program_options,system,thread,graph,regex
./bjam install
cd ~; /bin/rm -rf ~/boost_1_44_0*

# Cronolog - NEADED?? as added to apt-get above
cd ~
wget http://cronolog.org/download/cronolog-1.6.2.tar.gz
tar xzvf cronolog-1.6.2.tar.gz 
cd cronolog-1.6.2/
./configure; make; make install
cd ~; /bin/rm -rf cronolog-1.6.2*

# SIGAR
cd ~
wget http://www.hypertable.com/uploads/hyperic-sigar-1.6.4.zip
unzip hyperic-sigar-1.6.4.zip
cp hyperic-sigar-1.6.4/sigar-bin/include/*.h /usr/local/include
if [ $ARCH -eq 32 ]; then
  cp hyperic-sigar-1.6.4/sigar-bin/lib/libsigar-x86-linux.so /usr/local/lib
else
  cp hyperic-sigar-1.6.4/sigar-bin/lib/libsigar-amd64-linux.so /usr/local/lib
fi
/bin/rm -rf ~/hyperic-sigar-1.6.4*

# important to tell the lib sistem to index this
ldconfig

# BerkeleyDB
cd ~
wget http://www.hypertable.com/uploads/db-4.8.26.tar.gz
tar -xzvf db-4.8.26.tar.gz
cd db-4.8.26/build_unix/
../dist/configure --enable-cxx
make
make install
sh -c "echo '/usr/local/BerkeleyDB.4.8/lib' > /etc/ld.so.conf.d/BerkeleyDB.4.8.conf"
cd ~; /bin/rm -rf ~/db-4.8.26*

# Google RE2
cd ~
wget http://www.hypertable.com/uploads/re2.tgz
tar -zxvf re2.tgz
cd re2
# insert the missing include (manifests as a missing "ptrdiff_t does not name a type" message)
sed 's:<string.h>:<string.h>\
#include <cstddef>:' re2/stringpiece.h > sp.bak && mv sp.bak re2/stringpiece.h
make
make install
/bin/rm -rf ~/re2*

# libunwind
if [ $ARCH -eq 64 ]; then
  cd ~
  wget http://download.savannah.gnu.org/releases/libunwind/libunwind-1.0.1.tar.gz
  tar xzvf libunwind-1.0.1.tar.gz 
  cd libunwind-1.0.1/
  ./configure CFLAGS=-U_FORTIFY_SOURCE
  make
  make install
  cd ~
  /bin/rm -rf ~/libunwind-1.0.1*
fi

# Google Perftools
cd ~
wget http://google-perftools.googlecode.com/files/google-perftools-1.8.3.tar.gz
tar xzvf google-perftools-1.8.3.tar.gz
cd google-perftools-1.8.3
./configure
make
make install
cd ~
/bin/rm -rf ~/google-perftools-1.8.3*


# Google Snappy
cd ~
wget http://snappy.googlecode.com/files/snappy-1.0.4.tar.gz
tar xzvf snappy-1.0.4.tar.gz
cd snappy-1.0.4
./configure
make
make install
cd ~
/bin/rm -rf ~/snappy-1.0.4*

apt-get -y --allow-unauthenticated install openjdk-6-jdk


apt-get -y --allow-unauthenticated install ant autoconf automake libtool bison flex pkg-config php5 php5-dev php5-cli ruby-dev python-dev ruby1.8-dev libhttp-access2-ruby libbit-vector-perl libclass-accessor-chained-perl

# libevent4
cd ~
wget https://github.com/downloads/libevent/libevent/libevent-1.4.14b-stable.tar.gz
tar xzvf libevent-1.4.14b-stable.tar.gz 
cd libevent-1.4.14b-stable
./configure 
make
make install
cd ~; rm -rf libevent-1.4.14b-stable*

# Thrift
cd /usr/src
wget http://apache.mirror.anlx.net/thrift/0.8.0/thrift-0.8.0.tar.gz
tar xzvf thrift-0.7.0.tar.gz
rm -f thrift
ln -s thrift-0.7.0 thrift
cd thrift-0.7.0
chmod 755 ./configure ./lib/php/src/ext/thrift_protocol/build/shtool
./configure
make
make install

apt-get -y --allow-unauthenticated install dstat doxygen rrdtool graphviz gdb emacs rdoc rubygems

gem install capistrano sinatra rack thin json titleize

/sbin/ldconfig

