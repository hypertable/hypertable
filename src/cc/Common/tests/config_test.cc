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
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#include <Common/Compat.h>
#include <Common/Logger.h>
#include <Common/Properties.h>
#include <Common/Config.h>
#include <Common/Init.h>

#include <iostream>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace boost;
using namespace boost::program_options;
using namespace std;

bool sortfun(const boost::shared_ptr<option_description> &lhs,
        const boost::shared_ptr<option_description> &rhs) {
  return lhs->long_name() < rhs->long_name();
}

typedef std::vector< boost::shared_ptr<option_description> > OptVec;

int main(int argc, char *argv[]) {
  init(argc, argv);

  Desc desc = file_desc();
  OptVec &options = (OptVec &)desc.options();

  // sort alphabetically
  std::sort(options.begin(), options.end(), sortfun);

  for (size_t i = 0; i < options.size(); i++) {
    std::cout << "<hr>" << std::endl;
    boost::shared_ptr<const value_semantic> s = options[i]->semantic();
    cout << "<p><a name=\"" << options[i]->long_name() << "\"> </a></p>"
        << endl;
    cout << "<p><code>" << options[i]->long_name() << "</code></p>"
        << endl;
    cout << "<p style=\"margin-left: 40px; \">"
        << options[i]->description() << "</p>" << endl;

    boost::any def;
    if (s->apply_default(def)) {
      cout << "<p style=\"margin-left: 40px; \"><strong>Default "
          << "Value:</strong>&nbsp; " << Properties::to_str(def)
          << "</p>" << endl;
    }
  }

  return 0;
}
