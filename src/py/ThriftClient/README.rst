Hypertable clinet for python
============================

Check out the `documentation`__ for more complete examples.

.. __: http://hypertable.com/documentation/developer_guide/python/


Installation
------------

Install python hypertable client:

.. code-block:: console

    pip install hypertable


Creating a thrift client
------------------------

All of the examples in this document reference a pointer to a Thrift client object.  The following code snippet illustrates how to create a Thrift client object connected to a ThriftBroker listening on the default port (15867) on localhost.  To change the ThriftBroker location, just change "localhost" to the domain name of the machine on which the ThriftBroker is running.

.. code-block:: python

    try:
        client = ThriftClient("localhost", 15867)
    except Exception as e:
        print e
        sys.exit(1)
