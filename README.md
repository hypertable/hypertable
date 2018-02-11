HOW TO INSTALL
==============

You can either download an appropriate binary package for your
platform or build from source. Binary packages can be obtained from
[here](http://hypertable.com/download).

See [this wiki
page](http://code.google.com/p/hypertable/wiki/UpAndRunningWithBinaryPackages)
for getting started with hypertable binary packages.


HOW TO BUILD FROM SOURCE
========================

1.  Download the source:

    You can either download a release source tar ball from the [download
    page](http://hypertable.com/download) and unpack it in your
    source directory say ~/src:

        cd ~/src
        tar zxvf <path_to>/hypertable-<version>-src.tar.gz

    or from our git repository:

        cd ~/src
        git clone git://github.com/hypertable/hypertable.git

    From now on, we assume that your hypertable source tree is
    ~/src/hypertable

2.  Install the development environment:

    Run the following script to setup up the dev environment:

        ~/src/hypertable/bin/src-utils/htbuild --install dev_env

    If it did not work for your platform, check out the
    [HowToBuild](http://code.google.com/p/hypertable/wiki/HowToBuild)
    wiki for various tips on building on various platforms.

    Patches for htbuild to support your platforms are welcome :)

3.  Configure the build:

    Assuming you want your build tree to be ~/build/hypertable

        mkdir -p ~/build/hypertable
        cd ~/build/hypertable
        cmake ~/src/hypertable

    By default, hypertable gets installed in /opt/hypertable. To install into
    your own install directory, say $prefix, you can use:

        cmake -DCMAKE_INSTALL_PREFIX=$prefix ~/src/hypertable

    By default the build is configured for debug. To make a release build for
    production/performance test/benchmark:

        cmake -DCMAKE_BUILD_TYPE=Release ~/src/hypertable

    If you would like to install the build in a directory that contains
    a version suffix (e.g. 0.9.3.0.1d45f8d), you can configure as follows:

        cmake -DCMAKE_BUILD_TYPE=Release -DVERSION_ADD_COMMIT_SUFFIX=1 ~/src/hypertable

    If you would like to build OS specific packages in a directory that contains
    the specific OS and version (e.g. hypertable-0.9.5.0-suse_11.4-i386), you can configure as follows:

        cmake -DCMAKE_BUILD_TYPE=Release -DPACKAGE_OS_SPECIFIC=1 ~/src/hypertable

    Note, you can also use:

        ccmake ~/src/hypertable

    to change build parameters interactively.

    To build shared libraries, e.g., for scripting language extensions:

        cmake -DBUILD_SHARED_LIBS=ON ~/src/hypertable

    Since PHP has no builtin package system, its thrift installation needs to
    be manually specified for ThriftBroker support:
        cmake -DTHRIFT_SOURCE_DIR=/home/user/src/thrift
    
    Build support only for languages desired with option -DLANGS=pl,js,php,py2.pypy2.py3.pypy3 comma-separated-language ext+version. 
    Defaults to 'all'

4.  Build Hypertable binaries.

        make (or make -jnumber_of_cpu_or_cores_plus_1 for faster compile)
        make install

    Note, if it is a shared library install, you might need to do:

        echo $prefix/$version/lib | \
            sudo tee /etc/ld.so.conf.d/hypertable
        sudo /sbin/ldconfig

    Or, you can use the usual LD_LIBRARY_PATH (most Unix like OS) and
    DYLD_LIBRARY_PATH (Mac OS X) to specify non-standard shared library
    directories.


HOW TO MAKE BINARY PACKAGES
===========================

The available package generators for cmake are a little different on
different platforms. It is usually best to generate binary packages on
their native platforms.

    cd ~/build/hypertable
    ~/src/hypertable/bin/src-utils/htpkg generator...

where generator can be DEB (for debian packages), RPM (for
redhat/fedora/centos packages), PackageMaker (for Mac OS X packages)
and STGZ (for self extracting gzipped tar files.) etc. See cpack(1)
for more options.

For every generator listed on the command line, the above command will
generate two packages: one for the entire hypertable distribution and
one for thriftbroker only.

You can also use htbuild to generate binary packages on remote machines
that are not setup for development, even a base Amazon EC2 image:

    ~/src/hypertable/bin/src-utils/htbuild --ami ami-5946a730 DEB RPM TBZ2

At the moment, I recommend an Ubuntu/Debian machine for generating binary
packages as it can generate both RPM and DEB correctly faster without
having to disable prelinking and selinux like on Fedora/Redhat.


HOW TO RUN REGRESSION TESTS
===========================

1.  Make sure software is built and installed according to
    "HOW TO BUILD FROM SOURCE"

2.  Run the regression tests

        cd ~/build/hypertable
        make alltests

Note: there are two other high-level test targets: coretests (for core tests)
and moretests (for more longer running tests, which is included in alltests.)
These high-level test targets will (re)start test servers automatically when
new servers are installed.  There is also a low-level "test" target in root
of the build tree and src (and src/lang/Components) and tests/integration
directories that does NOT auto restart test servers.


HOW TO BUILD SOURCE CODE DOCUMENTATION TREE
===========================================

1.  Install the following tools:
    - [doxygen](http://www.stack.nl/~dimitri/doxygen/)
    - [graphviz](http://www.graphviz.org/)

    Note: if you ran `htbuild --install dev_env`, these should already
    be installed

2.  If you have doxygen installed on your system, then CMake should detect this
    and add a "doc" target to the make file.  Building the source code
    documentation tree is just a matter of running the following commands:

        cd ~/build/hypertable
        make doc

To view the docs, load the following file into a web browser:

-  `~/build/hypertable/doc/html/index.html` for source code documentation and,
-  `~/build/hypertable/gen-html/index.html` for Thrift API reference.
-  `~/build/hypertable/hqldoc/index.html` for HQL reference.

The Thrift API doc can also be generated independently (without doxygen
installed) by running the follow command in the build tree:

    make thriftdoc

Similarly HQL doc can be generated independently (without doxygen etc.):

    make hqldoc
