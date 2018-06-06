/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Common/Compat.h>
#include <Hypertable/Lib/Client.h>
using namespace Hypertable;

static Client *ht_client;
static NamespacePtr ht_namespace;

extern "C" {
#include <Python.h>

#include <sys/resource.h>


static PyObject *
is_bool(PyObject *self, PyObject *args)
{
	PyObject *is_it;

	if (!PyArg_ParseTuple(args, "s", &is_it))
		return NULL;
	
	if (is_it) {
		Py_RETURN_TRUE;
	} else {
		Py_RETURN_FALSE;
	}
}

static PyObject *
open_namespace(PyObject *self, PyObject *args)
{
	String ns;
	if (!PyArg_ParseTuple(args, "s", &ns))
		return NULL;
    ht_client = new Client();
    ht_namespace = ht_client->open_namespace(ns);
	return PyLong_FromVoidPtr((void *) &ht_namespace);
}

static PyMethodDef HtMethods[] =
{
	{"open_namespace", open_namespace, METH_VARARGS, "open_namespace('/') "},
	{"is_bool", is_bool, METH_VARARGS, "is_bool(var) "},
	{NULL, NULL, 0, NULL}
};



struct module_state {
	PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#else
#define GETSTATE(m) (&_state)
static struct module_state _state;
#endif


#if PY_MAJOR_VERSION >= 3

static int pyht_traverse(PyObject *m, visitproc visit, void *arg) {
	Py_VISIT(GETSTATE(m)->error);
	return 0;
}

static int pyht_clear(PyObject *m) {
	Py_CLEAR(GETSTATE(m)->error);
	return 0;
}

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"PyHypertable",
	NULL,
	sizeof(struct module_state),
	HtMethods,
	NULL,
	pyht_traverse,
	pyht_clear,
	NULL
};

PyMODINIT_FUNC
PyInit_PyHypertable(void)

#else

void
initPyHypertable(void)
#endif
{
#if PY_MAJOR_VERSION >= 3
	PyObject *module = PyModule_Create(&moduledef);
#else
	PyObject *module = Py_InitModule("PyHypertable", HtMethods);
#endif

	/* no core dump file */
	struct rlimit rlp;
	rlp.rlim_cur = 0;
	setrlimit(RLIMIT_CORE, &rlp);

#if PY_MAJOR_VERSION >= 3
	return module;
#endif
}
}