/**
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */
#include "Common/Compat.h"

#include "../ThriftBroker/SerializedCellsReader.h"
#include "../ThriftBroker/SerializedCellsWriter.h"

#include <boost/python.hpp>    // https://svn.boost.org/trac/boost/attachment/ticket/4125/4125-use-macros-instead-of-direct-access.2.patch
#include <pybind11/pybind11.h> // http ://pybind11.readthedocs.io/en/master/classes.html

using namespace Hypertable;


typedef bool (SerializedCellsWriter::*addfn)(const char *row,
	const char *column_family, const char *column_qualifier,
	int64_t timestamp, const char *value, int32_t value_length,
	int cell_flag);
typedef const char *(SerializedCellsWriter::*getfn)();
typedef int32_t(SerializedCellsWriter::*getlenfn)();

static addfn afn = &Hypertable::SerializedCellsWriter::add;
static getlenfn lenfn = &Hypertable::SerializedCellsWriter::get_buffer_length;

static PyObject *convert(const SerializedCellsWriter &scw) {
	boost::python::object obj(handle<>(PyBuffer_FromMemory(
		(void *)scw.get_buffer(), scw.get_buffer_length())));
	return boost::python::incref(obj.ptr());
}

namespace py = pybind11;


PYBIND11_MODULE(libHyperPythonMin,m)
{
	m.doc() = "Hypertable Minimal HyperPython Library libHyperPythonMin";
	//py::module m("libHyperPythonMin", R"pbdoc(
    //    Hypertable Minimal HyperPython Library libHyperPythonMin
    //)pbdoc");

	py::class_<Cell>(m, "Cell")
    .def("sanity_check", &Cell::sanity_check)
    .def_readwrite("row", &Cell::row_key)
    .def_readwrite("column_family", &Cell::column_family)
    .def_readwrite("column_qualifier", &Cell::column_qualifier)
    .def_readwrite("timestamp", &Cell::timestamp)
    .def_readwrite("revision", &Cell::revision)
    .def_readwrite("value", &Cell::value)
    .def_readwrite("flag", &Cell::flag)
    .def(self_ns::str(self_ns::self))
    ;

	py::class_<SerializedCellsReader>("SerializedCellsReader",init<const char *, uint32_t>())
    .def("has_next", &SerializedCellsReader::next)
    .def("get_cell", &SerializedCellsReader::get_cell,
          return_value_policy<return_by_value>())
    .def("row", &SerializedCellsReader::row,
          return_value_policy<return_by_value>())
    .def("column_family", &SerializedCellsReader::column_family,
          return_value_policy<return_by_value>())
    .def("column_qualifier", &SerializedCellsReader::column_qualifier,
          return_value_policy<return_by_value>())
    .def("value", &SerializedCellsReader::value_str,
          return_value_policy<return_by_value>())
    .def("value_len", &SerializedCellsReader::value_len)
    .def("value_str", &SerializedCellsReader::value_str,
          return_value_policy<return_by_value>())
    .def("timestamp", &SerializedCellsReader::timestamp)
    .def("cell_flag", &SerializedCellsReader::cell_flag)
    .def("flush", &SerializedCellsReader::flush)
    .def("eos", &SerializedCellsReader::eos)
  ;

  py::class_<SerializedCellsWriter, boost::noncopyable>("SerializedCellsWriter",init<int32_t, bool>())
    .def("add", afn)
    .def("finalize", &SerializedCellsWriter::finalize)
    .def("empty", &SerializedCellsWriter::empty)
    .def("clear", &SerializedCellsWriter::clear)
    .def("__len__", lenfn)
    .def("get", &convert)
  ;
}
