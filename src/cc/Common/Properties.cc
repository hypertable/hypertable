/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/** @file
 * Program options handling.
 * Based on boost::program_options, the options are configured in Config.h.
 */

#include <Common/Compat.h>
#include <Common/Properties.h>

#include <errno.h>
#include <fstream>

using namespace Hypertable;


void
Properties::load(const String &fname, PropertiesDesc &desc,
                 bool allow_unregistered) {
  std::ifstream in(fname.c_str());
  Config::Parser prs(in, desc, allow_unregistered);
  load_from(prs.get_options());
} 

void
Properties::load_files_by(const String &names_prop, PropertiesDesc &desc, 
                            bool allow_unregistered) {
  if(names_prop.empty() || !has(names_prop)) 
    return;

  Strings files = get_strs(names_prop);
  for (Strings::iterator it=files.begin(); it<files.end(); it++){
	  try {
      load(*it, desc, allow_unregistered);
    }
	  catch (std::exception &e) {
		  HT_WARNF("%s has bad cfg file %s: %s", 
                names_prop.c_str(), (*it).c_str(), e.what());
    }
  }
}

String 
Properties::reload(const String &fname, PropertiesDesc &desc, 
                   bool allow_unregistered) {
  String out;
	try {
		std::ifstream in(fname.c_str());

    HT_INFOF("Reloading Configuration File %s", fname.c_str());
		if (!in) throw;
    
    out.append("\n\nCurrent Configurations:\n");
    append_to_string(out);

    Config::Parser prs(in, desc, allow_unregistered);
    load_from(prs.get_options(), true);

    out.append("\n\nNew Configurations:\n");
    append_to_string(out);
    return out;
	}
	catch (std::exception &e) {
		HT_WARNF("Error::CONFIG_BAD_CFG_FILE %s: %s", fname.c_str(), e.what());
    return format("Error::CONFIG_BAD_CFG_FILE %s: %s \noutput:\n%s", 
                    fname.c_str(), e.what(), out.c_str());
	}
}

void
Properties::parse_args(int argc, char *argv[], PropertiesDesc &desc,
                       PropertiesDesc *hidden, bool allow_unregistered) {
  
  Config::Parser prs(argc, argv, desc, hidden, allow_unregistered);
  load_from(prs.get_options());
}

void
Properties::parse_args(const std::vector<String> &args,
                      PropertiesDesc &desc, PropertiesDesc *hidden,
                       bool allow_unregistered) {
  Config::Parser prs(args, desc, hidden, allow_unregistered);
  load_from(prs.get_options());
}
 
void 
Properties::load_from(Config::Parser::Options opts, bool only_guarded) {
  AliasMap::iterator it;
  for(const auto &kv : opts){
    if(has(kv.first) && (kv.second->is_default() 
                          || (only_guarded && !kv.second->is_guarded())))
      continue;

    set(kv.first, kv.second);
      
    it = m_alias_map.find(kv.first);
    if(it != m_alias_map.end())
      set((*it).second, get_value_ptr(kv.first));
  }
}

void Properties::alias(const String &primary, const String &secondary) {
  m_alias_map[primary] = secondary;
  m_alias_map[secondary] = primary;
}

void
Properties::print(std::ostream &out, bool include_default) {
  for (const auto &kv : m_map) {
    bool isdefault = kv.second->is_default();

    if (include_default || !isdefault) {
      out << kv.first << '=' << kv.second->str();

      if (isdefault)
        out << " (default)";
      out << std::endl;
    }
  }
}

void
Properties::append_to_string(String &out, bool include_default) {
  for (const auto &kv : m_map) {
    bool isdefault = kv.second->is_default();

    if (include_default || !isdefault) {
      out.append(format("%s=%s", kv.first.c_str(), kv.second->str().c_str()));

      if (isdefault)
        out.append(" (default)");
      out.append("\n");
    }
  }
}

