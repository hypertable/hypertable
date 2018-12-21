/* -*- c++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
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

#include <Common/Compat.h>

#include <Hypertable/Lib/Config.h>
#include <Hypertable/Lib/MetaLogDefinition.h>
#include <Hypertable/Lib/MetaLogEntity.h>
#include <Hypertable/Lib/MetaLogReader.h>
#include <Hypertable/Lib/MetaLogWriter.h>

#include <FsBroker/Lib/Client.h>

#include <AsyncComm/Comm.h>
#include <AsyncComm/ConnectionManager.h>
#include <AsyncComm/ReactorFactory.h>

#include <Common/FileUtils.h>
#include <Common/InetAddr.h>
#include <Common/Init.h>
#include <Common/Random.h>
#include <Common/Serialization.h>
#include <Common/StringExt.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>

using namespace Hypertable;
using namespace Config;
using namespace std;

namespace Hypertable {
  namespace MetaLog {

    class EntityGeneric : public Entity {
    public:
      EntityGeneric(int t) : Entity(t), m_value(100) {
        m_name = String("GenericEntity") + t;
        char buf[1025];
        memset(buf, '0', 1024);
        buf[1024] = 0;
        m_data.append(buf);
      }
      EntityGeneric(const EntityHeader &header_) : Entity(header_) { m_name = String("GenericEntity") + header_.type; }
      const String name() override { return m_name; }
      void display(ostream &os) override { os << "value=" << m_value; }
      void increment() { m_value++; }
      void set_value(int32_t value) { m_value = value; }

      void decode(const uint8_t **bufp, size_t *remainp,
                  uint16_t definition_version) override {
        Entity::decode(bufp, remainp);
      }

    private:

      uint8_t encoding_version() const override {
	return 1;
      }

      size_t encoded_length_internal() const override {
	return 4 + Serialization::encoded_length_vstr(m_data);;
      }

      void encode_internal(uint8_t **bufp) const override {
        Serialization::encode_i32(bufp, m_value);
        Serialization::encode_vstr(bufp, m_data);
      }

      void decode_internal(uint8_t version, const uint8_t **bufp,
			   size_t *remainp) override {
	(void)version;
        m_value = Serialization::decode_i32(bufp, remainp);
        m_data = Serialization::decode_vstr(bufp, remainp);
      }

      String m_name;
      int32_t m_value {};
      String m_data;
    };
    
    typedef std::shared_ptr<EntityGeneric> EntityGenericPtr;

    class TestDefinition : public Definition {
    public:
      TestDefinition() : Definition("bar") {}
      uint16_t version() override { return 1; }
      const char *name() override { return "foo"; }
      EntityPtr create(const EntityHeader &header) override {
        return make_shared<EntityGeneric>(header);
      }
    };

  }
}

namespace {

  struct MyPolicy : Policy {
    static void init_options() {
      cmdline_desc().add_options()
        ("save,s", "Don't delete generated the log files")
        ;
    }
  };

  typedef Meta::list<MyPolicy, FsClientPolicy, DefaultCommPolicy> Policies;

  MetaLog::DefinitionPtr g_test_definition = make_shared<MetaLog::TestDefinition>();

  vector<MetaLog::EntityPtr> g_entities;

  void create_entities(int count) {
    for (size_t i=65536; i<=(size_t)65536+count; i++)
      g_entities.push_back( make_shared<MetaLog::EntityGeneric>(i) );
  }

  void randomly_change_states(MetaLog::WriterPtr &writer) {    
    size_t j;
    for (size_t i=0; i<256; i++) {
      j = Random::number32() % g_entities.size();
      if (g_entities[j]) {
        dynamic_pointer_cast<MetaLog::EntityGeneric>(g_entities[j])->increment();
        if ((i%127)==0) {
          writer->record_removal(g_entities[j]);
          g_entities[j] = 0;
        }
        else
          writer->record_state(g_entities[j]);
      }
    }
  }

  void randomly_set_values(MetaLog::WriterPtr &writer) {
    for (auto & entity : g_entities)
      dynamic_pointer_cast<MetaLog::EntityGeneric>(entity)->set_value( Random::number32() % 1000000 );
    writer->record_state(g_entities);
  }

  void display_entities(ofstream &out) {
    for (size_t i=0; i<g_entities.size(); i++) {
      if (g_entities[i])
        out << *g_entities[i] << "\n";
    }
    out << flush;
  }

} // local namespace


int
main(int ac, char *av[]) {
  try {
    init_with_policies<Policies>(ac, av);

    Random::seed(1);

    int timeout = has("fs-timeout") ? get_i32("fs-timeout") : 180000;
    String host = get_str("fs-host");
    uint16_t port = get_i16("fs-port");

    FsBroker::Lib::ClientPtr client = std::make_shared<FsBroker::Lib::Client>(host, port, timeout);

    if (!client->wait_for_connection(timeout)) {
      HT_ERROR_OUT <<"Unable to connect to FS: "<< host <<':'<< port << HT_END;
      return 1;
    }

    FilesystemPtr fs = client;

    String testdir = format("/metalog%09d", (int)getpid());
    MetaLog::WriterPtr writer;
    MetaLog::ReaderPtr reader;

    fs->mkdirs(testdir);

    MetaLog::EntityHeader::display_timestamp = false;

    /**
     *  Write initital log
     */

    writer = make_shared<MetaLog::Writer>(fs, g_test_definition,
                                          testdir + "/" + g_test_definition->name(),
                                          g_entities);

    create_entities(32);

    {
      ofstream out("metalog_test.out");
      randomly_change_states(writer);
      writer = 0;
      reader = make_shared<MetaLog::Reader>(fs, g_test_definition,
                                            testdir + "/" + g_test_definition->name());
      g_entities.clear();
      reader->get_entities(g_entities);
      display_entities(out);
      reader.reset();
      out.close();
      HT_ASSERT(system("diff metalog_test.out metalog_test.golden") == 0);
    }

    /**
     *  Write some more
     */

    writer = make_shared<MetaLog::Writer>(fs, g_test_definition,
                                          testdir + "/" + g_test_definition->name(),
                                          g_entities);

    {
      ofstream out("metalog_test2.out");
      randomly_change_states(writer);
      writer.reset();
      reader = make_shared<MetaLog::Reader>(fs, g_test_definition,
                                            testdir + "/" + g_test_definition->name());
      g_entities.clear();
      reader->get_entities(g_entities);
      display_entities(out);
      reader.reset();
      out.close();
      HT_ASSERT(system("diff metalog_test2.out metalog_test2.golden") == 0);
    }


    /**
     *  Log file rollover test
     */
    properties->set("Hypertable.MetaLog.MaxFileSize", (int64_t)50000);
    writer = make_shared<MetaLog::Writer>(fs, g_test_definition,
                                          testdir + "/" + g_test_definition->name(),
                                          g_entities);

    {
      ofstream out("metalog_test3.out");
      randomly_set_values(writer);
      randomly_set_values(writer);
      randomly_set_values(writer);
      randomly_set_values(writer);
      writer.reset();
      reader = make_shared<MetaLog::Reader>(fs, g_test_definition,
                                            testdir + "/" + g_test_definition->name());
      g_entities.clear();
      reader->get_entities(g_entities);
      display_entities(out);
      reader.reset();
      out.close();
      HT_ASSERT(system("diff metalog_test3.out metalog_test3.golden") == 0);
    }

    /**
     *  Write another log and skip the RECOVER entry
     */

    MetaLog::Writer::skip_recover_entry = true;

    writer = make_shared<MetaLog::Writer>(fs, g_test_definition,
                                          testdir + "/" + g_test_definition->name(),
                                          g_entities);
    writer.reset();

    try {
      reader =
        make_shared<MetaLog::Reader>(fs, g_test_definition,
                                     testdir + "/" + g_test_definition->name());
      HT_ASSERT(!"METALOG missing RECOVER entity exception not thrown");
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
    }

    if (!has("save"))
      fs->rmdir(testdir);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    return 1;
  }
  return 0;
}
