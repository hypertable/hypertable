/*
 * Copyright (C) 2011 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */


#include <Hypertable/Lib/Client.h>
#include <Common/Serializable.h>

namespace ht = Hypertable;


#include <pybind11/pybind11.h>
namespace py = pybind11;


PYBIND11_MODULE(hypertable_client, m) {
  m.doc() = "Python Hypertable Client Package";

  py::class_<ht::Namespace, ht::NamespacePtr>(m, "Namespace");

  //py::class_<ht::HqlInterpreter, ht::HqlInterpreterPtr>(m, "HqlInterpreter");
  // interp->set_namespace( name );
  // interp->execute( hql, cb); /  hql->execute("use '/'");

  py::class_<ht::Client, ht::ClientPtr>(m, "HypertableClient")
    .def(py::init<std::string, uint32_t>(),
          py::arg("install_dir"),
          py::arg("default_timeout_ms") = (uint32_t)10000
          )
    .def(py::init<std::string, std::string, uint32_t>(),
          py::arg("install_dir"),
          py::arg("config_file"),
          py::arg("default_timeout_ms") = (uint32_t)10000
          )
    .def("create_namespace", &ht::Client::create_namespace, 
           py::arg("name"), 
           py::arg("base") = (ht::NamespacePtr)nullptr,
           py::arg("create_intermediate") = false,
           py::arg("if_not_exists") = false
           )
    .def("exists_namespace", &ht::Client::exists_namespace,
           py::arg("name"), 
           py::arg("base") = (ht::NamespacePtr)nullptr
           )
    .def("open_namespace", &ht::Client::open_namespace, 
           py::arg("name"), 
           py::arg("base") = (ht::NamespacePtr)nullptr
           )
    .def("drop_namespace", &ht::Client::drop_namespace, 
           py::arg("name") = py::bytes(""), 
           py::arg("base") = (ht::NamespacePtr)nullptr,
           py::arg("if_exists") = false
           )
   // .def("create_hql_interpreter", ht::Client::create_hql_interpreter,
   //        py::arg("immutable_namespace") = true
   //        )
    ;

}


/*
    ht_namespace = ht_client->open_namespace("/");

    TablePtr table = ht_namespace->open_table("LoadTest");

    // scan all keys with the index
    ScanSpecBuilder ssb;
    ssb.add_column("Field1");
    ssb.add_column_predicate("Field1", "", ColumnPredicate::PREFIX_MATCH, "");
    TableScanner *ts = table->create_scanner(ssb.get());
    Cell cell;
    int i = 0;
    while (ts->next(cell)) {
      if (++i > 10)
        break;
    }
*/