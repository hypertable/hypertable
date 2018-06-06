STATE:

WITH "make PyHypertable":

import imp

ht=imp.load_dynamic('PyHypertable', '/root/builds/builts/hypertable/src/py/libpyhypertable/libPyHypertable.so')

 dir(ht)
 
 ['__doc__', '__file__', '__name__', '__package__', 'is_bool', 'open_namespace']
 
 dir(ht.open_namespace('/'))
 
 terminate called after throwing an instance of 'Hypertable::Exception'
 
   what():  getting value of 'Hyperspace.Replica.Host': boost::bad_any_cast: failed conversion using boost::any_cast
   
 Aborted

--> THE SAME ERROR IS WITH BOOST-PYTHON with PythonBinding





WITH "cd /root/builds/sources/hypertable/src/py/libpyhypertable/; python setup.py install"
 
 import PyHypertable
 
 Traceback (most recent call last):
 
   File "<stdin>", line 1, in <module>
 
 ImportError: /usr/local/lib/python2.7/site-packages/PyHypertable.so: undefined symbol:   
 _ZN10Hypertable17SleepWakeNotifierC1ESt8functionIFvvEES3_


--> Should it be instead many static/shared libs a wrapped one shared lib?
