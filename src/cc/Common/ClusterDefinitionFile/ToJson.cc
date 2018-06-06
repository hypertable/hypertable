/*
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

/// @file
/// Definitions for ToJson.
/// This file contains type definitions for ToJson, a class for
/// converting a cluster definition file into JSON.

#include <Common/Compat.h>

#include "ToJson.h"
#include "Tokenizer.h"

#include <Common/FileUtils.h>
#include <Common/HostSpecification.h>
#include <Common/Logger.h>
#include <Common/String.h>
#include <Common/System.h>
#include <Common/Version.h>

#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stack>
#include <string>
#include <vector>

extern "C" {
#include <netdb.h>
#include <pwd.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

#if defined(__APPLE__)
extern char **environ;
#endif

using namespace Hypertable;
using namespace Hypertable::ClusterDefinitionFile;
using namespace std;

namespace {

  class Node {
  public:
    Node(const string &n) : name(n) {}
    string name;
    set<string> roles;
  };

  class Definition {
  public:
    void add_role(const string &text) {
      const char *base = text.c_str();
      const char *ptr;
      for (ptr=base; *ptr && !isspace(*ptr); ptr++)
        ;
      string token(base, ptr-base);
      boost::trim(token);
      if (strcasecmp(token.c_str(), "role:"))
        HT_THROWF(Error::SYNTAX_ERROR, "Mal-formed role definition: %s", text.c_str());
      while (*ptr && isspace(*ptr))
        ptr++;
      base = ptr;
      while (*ptr && !isspace(*ptr))
        ptr++;
      string name(base, ptr-base);
      boost::trim(name);
      if (name.empty())
        HT_THROWF(Error::SYNTAX_ERROR, "Mal-formed role definition: %s", text.c_str());
      string input(ptr);
      string value;

      boost::char_separator<char> sep(" \t\n\r");
      boost::tokenizer< boost::char_separator<char> > tokens(input, sep);
      for (auto &t : tokens)
        value += t + " ";
      boost::trim(value);

      m_roles[name] = value;

      if (!m_uber_hostspec.empty())
        m_uber_hostspec += " + ";
      m_uber_hostspec += "(" + value + ")";
    }

    string to_json() {
      map<string, Node *> node_map;

      // Populate m_nodes and node_map
      vector<string> nodes = HostSpecification(m_uber_hostspec);
      m_nodes.reserve(nodes.size());
      for (auto & node : nodes) {
        m_nodes.push_back(std::make_unique<Node>(node));
        node_map[node] = m_nodes.back().get();
      }

      bool first {};
      string str = "{\n";

      // roles
      {
        first = true;
        str += "  \"roles\": {\n";
        for (auto entry : m_roles) {
          if (!first)
            str += ",\n";
          else
            first = false;
          str += "    \"" + entry.first + "\": \"" + entry.second + "\"";
          vector<string> nodes = HostSpecification(entry.second);
          for (auto & node : nodes)
            node_map[node]->roles.insert(entry.first);
        }
        str += "\n  },\n";
      }

      // nodes
      first = true;
      str += "  \"nodes\": [\n";
      for (auto & node : m_nodes) {
        if (!first)
          str += ",\n";
        else
          first = false;
        struct hostent *hent = gethostbyname(node->name.c_str());
        str.append("    { \"hostname\": \"");
        str.append((const char *)(hent->h_name));
        str.append("\", \"roles\": [");
        bool first2 {true};
        for (auto & role : node->roles) {
          if (first2)
            first2 = false;
          else
            str += ", ";
          str += "\"" + role + "\"";
        }
        str += "] }";
      }
      str += "\n  ]\n";

      str += "}";
      return str;
    }

  private:
    string m_uber_hostspec;
    map<string, string> m_roles;
    vector<unique_ptr<Node>> m_nodes;
  };

}


ToJson::ToJson(const string &fname) : m_definition_file(fname) {

  // Create map of environment variables
  map<string, string> environ_map;
  for (size_t i=0; environ[i]; i++) {
    const char *ptr = strchr(environ[i], '=');
    HT_ASSERT(ptr);
    environ_map[string(environ[i], ptr-environ[i])] = string(ptr+1);
  }

  stack<TokenizerPtr> definitions;

  definitions.push( make_shared<Tokenizer>(m_definition_file) );

  Token token;
  Definition def;

  while (!definitions.empty()) {
    while (definitions.top()->next(token)) {
      if (token.type == Token::ROLE)
        def.add_role(token.text);
    }
    definitions.pop();
  }

  m_str = def.to_json();
}

