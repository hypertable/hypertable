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

/** @file
 * Initialization helper for applications.
 *
 * The initialization routines help setting up application boilerplate,
 * configuration policies and usage strings.
 */

#ifndef Common_Init_h
#define Common_Init_h

#include <Common/Config.h>
#include <Common/System.h>

namespace Hypertable { namespace Config {

  /** @addtogroup Common
   *  @{
   */

  /**
   * Init with policy (with init_options (called before parse_args) and
   * init (called after parse_args) methods. The Policy template class is
   * usually a list of configuration policies, i.e.
   *
   *    typedef Meta::list<Policy1, Policy2, ...> MyPolicy;
   *
   * @sa Config::parse_args for params.
   *
   * @param argc The argc parameter of the main() function
   * @param argv The argv parameter of the main() function
   * @param desc Optional command option descriptor
   */
  template <class PolicyT>
  inline void init_with_policy(int argc, char *argv[], Desc *desc = 0) {
    try {
      System::initialize();

      std::lock_guard<std::recursive_mutex> lock(rec_mutex);
      properties = std::make_shared<Properties>();

      if (desc)
        cmdline_desc(*desc);

      PolicyT::init_options();
      parse_args(argc, argv);
      PolicyT::init();

      if (get<gBool>("verbose"))
        properties->print(std::cout);
    }
    catch (Exception &e) {
      PolicyT::on_init_error(e);
    }
  }

  /** Convenience function (more of a demo) to init with a list of polices
   * @sa init_with
   *
   * @param argc The argc parameter of the main() function
   * @param argv The argv parameter of the main() function
   * @param desc Optional command option descriptor
   */
  template <class PolicyListT>
  inline void init_with_policies(int argc, char *argv[], Desc *desc = 0) {
    typedef typename Join<PolicyListT>::type Combined;
    init_with_policy<Combined>(argc, argv, desc);
  }

  /** Initialize with default policy
   *
   * @param argc The argc parameter of the main() function
   * @param argv The argv parameter of the main() function
   * @param desc Optional command option descriptor
   */
  inline void init(int argc, char *argv[], Desc *desc = NULL) {
    init_with_policy<DefaultPolicy>(argc, argv, desc);
  }

  /** @} */

}}

#endif /* Common_Init_h */
