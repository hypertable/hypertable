/** -*- C++ -*-
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
#include "Common/System.h"

#include <iostream>
#include <fstream>
#include "ThriftBroker/Client.h"
#include "ThriftBroker/gen-cpp/HqlService.h"
#include "ThriftBroker/ThriftHelper.h"
#include "ThriftBroker/SerializedCellsReader.h"
#include "ThriftBroker/SerializedCellsWriter.h"

using namespace Hypertable;
using namespace Hypertable::ThriftGen;

/**
 * This test demonstrates the use of the
 * SerializedCellsReader/SerializedCellsWriter APIs.
 *
 * It assumes that there's a namespace "test" with a table "thrift_test".
 *
 * HQL setup:
 *     CREATE NAMESPACE test;
 *     CREATE TABLE thrift_test (col);
 */

void flush_and_clear(Thrift::Client *client, Mutator mutator,
        SerializedCellsWriter &scw) {
  // the SerializedCellsWriter is full - now send the data to the
  // ThriftBroker
  const uint8_t *ptr;
  int32_t len;
  scw.get_buffer(&ptr, &len);
  CellsSerialized serialized((const char *)ptr, len);
  client->mutator_set_cells_serialized(mutator, serialized, true);
      
  // clear() the SerializedCellsWriter and continue adding more rows
  scw.clear();
}

void test_writer(Thrift::Client *client) {
  Namespace ns = client->namespace_open("test");
  Mutator mutator = client->mutator_open(ns, "thrift_test", 0, 0);

  std::vector<String> rows;
  for (int i = 0; i < 10000; i++) {
    char buffer[64];
    sprintf(buffer, "row%08d", i);
    rows.push_back(buffer);
  }

  // create a SerializedCellsWriter with a buffer of 1MB and add as many
  // cells till the buffer is full. You can increase the buffer size if your
  // cells are larger.
  SerializedCellsWriter scw(1024 * 1024);
  for (std::vector<String>::iterator it = rows.begin();
          it != rows.end(); ++it) {
    if (!scw.add((*it).c_str(), "col", /* column qualifier */0,
                Hypertable::AUTO_ASSIGN, "value", strlen("value") + 1))
      flush_and_clear(client, mutator, scw);
  }

  // send the remaining cells
  flush_and_clear(client, mutator, scw);

  // clean up
  client->mutator_close(mutator);
  client->namespace_close(ns);
}

void test_reader(Thrift::Client *client) {
  ScanSpec scanspec;
  Namespace ns = client->namespace_open("test");
  Scanner scanner = client->scanner_open(ns, "thrift_test", scanspec);

  ResultSerialized results;

  // get the first batch of cells
  std::string raw_results;
  client->scanner_get_cells_serialized(raw_results, scanner);

  // keep looping while we have cells to process
  while (raw_results.size()) {
    SerializedCellsReader reader((void *)raw_results.c_str(),
            raw_results.length());

    Hypertable::Cell hcell;
    while (reader.next()) {
      reader.get(hcell);
      std::cout << hcell << std::endl;
    }

    // reached end of stream?
    if (reader.eos())
      break;
  
    // fetch the next results
    client->next_cells_serialized(raw_results, scanner);
  }

  client->scanner_close(scanner);
  client->namespace_close(ns);
}

int main(int argc, char** argv) {
  Thrift::Transport ttp = Thrift::Transport::FRAMED;
  if (argc > 1) {
	if (strcmp(argv[1], "zlib") == 0)
      ttp = Thrift::Transport::ZLIB;
  }
  try {
	// connect to the local ThriftBroker
	Thrift::Client *client = new Thrift::Client(ttp, "localhost", 15867);

    // insert a couple of cells using the SerializedCellsWriter
    test_writer(client);

    // then fetch them using the SerializedCellsReader
    test_reader(client);
  }
  catch (Thrift::TException &ex) {
    std::cout << "Caught an exception! Next steps: " << std::endl
        << "\t- make sure that the namespace 'thrift' exists" << std::endl
        << "\t- make sure that the table 'thrift_test(col)' exists" << std::endl
        << "\t- check the ThriftBroker.log file for errors" << std::endl;
    return -1;
  }

  return 0;
}

