/*
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
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
 * Configuration settings.
 * This file contains the global configuration settings (read from file or
 * from a command line parameter).
 */

#ifndef Common_Config_h
#define Common_Config_h

#include <Common/Logger.h>
#include <Common/Meta.h>
#include <Common/Properties.h>

#include <mutex>

namespace Hypertable { namespace Config {

  /** @addtogroup Common
   *  @{
   */

  typedef PropertiesDesc Desc;

  /** A global (recursive) configuration mutex */
  extern std::recursive_mutex rec_mutex;

  /** This singleton map stores all options */
  extern PropertiesPtr properties;

  /** Check existence of a configuration value
   *
   * @param name The name of the option to search for
   * @return true if there is an option with this name
   */
  bool has(const String &name);

  /** Check if a configuration value is defaulted
   *
   * @param name The name of the option
   * @return true if this option's value is the default value
   */
  bool defaulted(const String &name);

  /** Retrieves a configuration value
   *
   * This is a template function and usually not used directly. The file
   * Properties.h provides global functions like get_bool(), get_string()
   * that are usually used.
   *
   * @param name The name of the option
   * @return The option's value
   */
  template <typename T>
  T get(const String &name) {
    HT_ASSERT(properties);
    return properties->get<T>(name);
  }

  /** Retrieves a configuration value (or a default value, if the value
   * was not set)
   *
   * @param name The name of the option
   * @param default_value The default value which is returned if the value was
   *        not set
   * @return The option's value
   */
  template <typename T>
  T get(const String &name, T default_value) {
    HT_ASSERT(properties);
    return properties->get<T>(name, default_value);
  }

  /** A macro which definds global functions like get_bool(), get_str(),
   * get_i16() etc. @see Properties.h */
  HT_PROPERTIES_ABBR_ACCESSORS()

  /**
   * Get the command line options description
   *
   * @param usage Optional usage string (first time)
   * @return Reference to the Description object
   */
  Desc &cmdline_desc(const char *usage=nullptr);

  /** Set the command line options description
   *
   * @param desc Reference to the Description object
   */
  void cmdline_desc(Desc &desc);

  /** Get the command line hidden options description (for positional options)
   * @return desc Reference to the hidden description object
   */
  Desc &cmdline_hidden_desc();

  /** Get the config file options description
   *
   * @param usage - optional usage string
   * @return Reference to the Description object
   */
  Desc &file_desc(const char *usage = NULL);

  /** Set the config file options description
   *
   * @param desc Reference to the Description object
   */
  void file_desc(Desc &desc);

  /**
   * Initialization helper; parses the argc/argv parameters into properties,
   * reads the configuration file, handles "help" and "help-config" parameters
   *
   * @param argc Number of elements in argv
   * @param argv Name of binary and command line arguments
   */
  void parse_args(int argc, char *argv[]);

  /**
   * Parses a configuration file and stores all configuration options into
   * the option descriptor
   *
   * @param fname The filename of the configuration file
   * @param desc Reference to the Description object
   * @throws Error::CONFIG_BAD_CFG_FILE on error
   */
  void parse_file(const String &fname, Desc &desc);

  String reparse_file(const String &fname);

  /**
   * Setup command line option alias for config file option.
   * Typically used in policy init_options functions.
   * The command line option has higher priority.
   *
   * Requires use of sync_alias() afterwards.
   *
   * @param cmdline_opt Command line option name
   * @param file_opt Configuration file option name
   * @param overwrite If true then existing aliases are overwritten
   */
  void alias(const String &cmdline_opt, const String &file_opt);

  /**
   * Toggle allow unregistered options. By default unregistered options
   * are not allowed.
   *
   * @param choice If true then unregistered options are allowed, otherwise not
   * @return The previous value
   */
  bool allow_unregistered_options(bool choice);

  /** Returns true if unregistered options are allowed
   *
   * @return true if unregistered options are allowed, otherwise false
   */
  bool allow_unregistered_options();

  /**
   * Free all resources used
   */
  void cleanup();


  
  /**
   * Interface and base of config policy
   *
   * The configuration Policy describes application-specific configuration
   * options and initialization routines. Multiple Policies can be used
   * in a single application.
   */
  struct Policy {
    static void init_options() { }
    static void init() { }
    static void on_init_error(Exception &e) {
      HT_ERROR_OUT << e << HT_END;
      std::exit(EXIT_FAILURE);
    }
    static void cleanup() { }
  };

  /**
   * Default init policy
   *
   * The DefaultPolicy sets up the regular Hypertable configuration options
   * and initializes the logging module.
   */
  struct DefaultPolicy : Policy {
    static void init_options();
    static void init();
  };

  /**
   * Helpers to compose init policies; allow to combine two policies into one
   */
  template <class CarT, class CdrT>
  struct Cons {
    static void init_options() {
      CarT::init_options();
      CdrT::init_options();
    }
    static void init() {
      CarT::init();
      CdrT::init();
    }
    static void on_init_error(Exception &e) {
      CarT::on_init_error(e);
      CdrT::on_init_error(e);
    }
    static void cleanup() {
      CdrT::cleanup();  // in reverse order
      CarT::cleanup();
    }
  };

  struct NullPolicy { };

  // Partial specialization for type list algorithms
  template <class PolicyT>
  struct Cons<NullPolicy, PolicyT> {
    static void init_options() { PolicyT::init_options(); }
    static void init() { PolicyT::init(); }
    static void on_init_error(Exception &e) { PolicyT::on_init_error(e); }
    static void cleanup() { PolicyT::cleanup(); }
  };

  // Conversion from policy list to combined policy
  template <class PolicyListT>
  struct Join {
    typedef typename Meta::fold<PolicyListT, NullPolicy,
            Cons<Meta::_1, Meta::_2> >::type type;
  };



  /** @}*/

}}

#endif // Common_Config_h
