Hypertable clinet for python
============================

Check out the `documentation`__ for more complete examples.

.. __: http://hypertable.com/documentation/developer_guide/python/


Installation
------------

Install python hypertable client:

.. code-block:: console

    pip install hypertable
    
A dist package is available at the build dir ./src/py/ht-package/pkg/dist/hypertable-VERSION.tar.gz after make install
(It can be unpacked to Hypertable install dir ./lib/py or remain as site-packages installation)

The hyperthrift Thrift's generated files module need to be part of the package for the protocol version consistency, if to use not equivalent protocol versions, errors such as serialization version might occur, and should be bounded by the thrift requirement version on with the pkg requirements.txt file. (pip install hypertable-0.9.8.11.tar.gz -r requirements.txt)

Creating a thrift client
------------------------

All of the examples in this document reference a pointer to a Thrift client object.  The following code snippet illustrates how to create a Thrift client object connected to a ThriftBroker listening on the default port (15867) on localhost.  To change the ThriftBroker location, just change "localhost" to the domain name of the machine on which the ThriftBroker is running.

.. code-block:: python


    from hypertable.thrift_client.thriftclient import ThriftClient
    from hypertable.thrift_client.serialized_cells import Reader, Writer
    from hypertable.thrift_client.hyperthrift.gen.ttypes import *

    try:
        client = ThriftClient("localhost", 15867)
    except Exception as e:
        print e
        sys.exit(1)

