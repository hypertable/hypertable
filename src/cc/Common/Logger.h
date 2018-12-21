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
 * Logging routines and macros.
 * The LogWriter provides facilities to write debug, log, error- and other
 * messages to stdout. The Logging namespaces provides std::ostream-
 * and printf-like macros and convenience functions.
 */

#ifndef Common_Logger_h
#define Common_Logger_h

#include "Error.h"
#include "String.h"

#include <iostream>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <atomic>

#include "FixedStream.h"

namespace Hypertable { 

/** Logging framework. */
namespace Logger {

  /** @addtogroup Common
   *  @{
   */

  /** Output priorities modelled after syslog */
  namespace Priority {
    enum {
      EMERG  = 0,
      FATAL  = 0,
      ALERT  = 1,
      CRIT   = 2,
      ERROR  = 3,
      WARN   = 4,
      NOTICE = 5,
      INFO   = 6,
      DEBUG  = 7,
      NOTSET = 8
    };
  } // namespace Priority


  /** Property Extended Enum Cfg calls */
  namespace cfg {

    int from_string(String loglevel);

    String repr(int value);

  } // namespace cfg


  /** The LogWriter class writes to stdout. It's not used directly, but
   * rather through the macros below (i.e. HT_ERROR_OUT, HT_ERRORF etc).
   */
  class LogWriter {
    public:
      /** Constructor
       *
       * @param name The name of the application
       */
      LogWriter(const String &name)
        : m_show_line_numbers(true), m_test_mode(false), m_name(name),
          m_priority(Priority::INFO), m_file(stdout) {
      }

      /** Sets the message level; all messages with a higher level are discarded
       */
      void set_level(int level) {
        m_priority = level;
      }

      /** Returns the message level */
      int  get_level() const {
        return m_priority;
      }

      /** Returns true if a message with this level is not discarded */
      bool is_enabled(int level) const {
        return level <= m_priority;
      }

      /** The test mode disables line numbers and timestamps and can
       * redirect the output to a separate file descriptor
       */
      void set_test_mode(int fd = -1) {
        if (fd != -1)
          m_file = fdopen(fd, "wt");
        m_show_line_numbers = false;
        m_test_mode = true;
      }

      /** Returns true if line numbers are printed */ 
      bool show_line_numbers() const {
        return m_show_line_numbers;
      }

      /** Flushes the log file */
      void flush() {
        fflush(m_file);
      }

      /** Prints a debug message with variable arguments (similar to printf) */
      void debug(const char *format, ...);

      /** Prints a debug message */
      void debug(const String &message) {
        log(Priority::DEBUG, message);
      }

      /** Prints a message with variable arguments */
      void log(int priority, const char *format, ...);

      /** Prints a message */
      void log(int priority, const String &message) {
        log_string(priority, message.c_str());
      }

    private:
      /** Appends a string message to the log */
      void log_string(int priority, const char *message);

      /** Appends a string message with variable arguments to the log */
      void log_varargs(int priority, const char *format, va_list ap);

      /** True if line numbers are shown */
      bool m_show_line_numbers;

      /** True if this log is in test mode */
      bool m_test_mode;

      /** The name of the application */
      String m_name;

      /** The current priority (everything above is filtered) */
      std::atomic<int> m_priority;

      /** The output file handle */
      FILE *m_file;
  };

  /** Public initialization function - creates a singleton instance of
   * LogWriter
   */
  extern void initialize(const String &name);

  /** Accessor for the LogWriter singleton instance */
  extern LogWriter *get();

  /** @} */

}} // namespace Hypertable::Logger


#define HT_LOG_BUFSZ 1024

/* The HT_ABORT macro terminates the application and generates a core dump */
#ifdef HT_USE_ABORT
#  define HT_ABORT abort()
#else
#define HT_ABORT raise(SIGABRT)
#endif

// printf interface macro helper; do not use directly
#define HT_LOG(priority, msg) do { \
  if (Logger::get()->is_enabled(priority)) { \
    if (Logger::get()->show_line_numbers()) \
      Logger::get()->log(priority, Hypertable::format( \
          "(%s:%d) %s", __FILE__, __LINE__, msg)); \
    else \
      Logger::get()->log(priority, msg); \
  } \
} while (0)

#define HT_LOGF(priority, fmt, ...) do { \
  if (Logger::get()->is_enabled(priority)) { \
    if (Logger::get()->show_line_numbers()) \
      Logger::get()->log(priority, Hypertable::format( \
          "(%s:%d) " fmt, __FILE__, __LINE__, __VA_ARGS__)); \
    else \
      Logger::get()->log(priority, Hypertable::format( \
          fmt, __VA_ARGS__));  \
  } \
} while (0)

// stream interface macro helpers
#define HT_LOG_BUF_SIZE 4096

#define HT_OUT(priority) do { if (Logger::get()->is_enabled(priority)) { \
  char logbuf[HT_LOG_BUF_SIZE]; \
  int _priority_ = Logger::get()->get_level(); \
  FixedOstream _out_(logbuf, sizeof(logbuf)); \
  if (Logger::get()->show_line_numbers()) \
    _out_ <<"("<< __FILE__ <<':'<< __LINE__ <<") "; \
  _out_

#define HT_OUT2(priority) do { if (Logger::get()->is_enabled(priority)) { \
  char logbuf[HT_LOG_BUF_SIZE]; \
  int _priority_ = priority; \
  FixedOstream _out_(logbuf, sizeof(logbuf)); \
  _out_ << __func__; \
  if (Logger::get()->show_line_numbers()) \
    _out_ << " ("<< __FILE__ <<':'<< __LINE__ <<")"; \
  _out_ <<": "

#define HT_END ""; Logger::get()->log(_priority_, _out_.str()); \
  if (_priority_ == Logger::Priority::FATAL) HT_ABORT; \
} /* if enabled */ } while (0)

#define HT_OUT_DISABLED do { if (0) {

// helpers for printing a char pointer field
#define HT_DUMP_CSTR(_os_, _label_, _str_) do { \
  if (!_str_) _os_ <<" " #_label_ "=[NULL]"; \
  else _os_ <<" " #_label_ "='"<< (_str_) << "'"; \
} while (0)

#define HT_DUMP_CSTR_FIELD(_os_, _obj_, _field_) \
  HT_DUMP_CSTR(_os_, _field_, _obj_._field_)


// Logging macros interface starts here
#ifndef HT_DISABLE_LOG_ALL

#ifndef HT_DISABLE_LOG_DEBUG

#define HT_LOG_ENTER do { \
  if (Logger::get()->is_enabled(Logger::Priority::DEBUG)) {\
    if (Logger::get()->show_line_numbers()) \
      Logger::get()->debug("(%s:%d) %s() ENTER", __FILE__, __LINE__, HT_FUNC);\
    else \
      Logger::get()->debug("%s() ENTER", HT_FUNC); \
  } \
} while(0)

#define HT_LOG_EXIT do { \
  if (Logger::get()->is_enabled(Logger::Priority::DEBUG)) { \
    if (Logger::get()->show_line_numbers()) \
      Logger::get()->debug("(%s:%d) %s() EXIT", __FILE__, __LINE__, HT_FUNC); \
    else \
      Logger::get()->debug("%s() EXIT", HT_FUNC); \
  } \
} while(0)

#define HT_DEBUG(msg) HT_LOG(Logger::Priority::DEBUG, msg)
#define HT_DEBUGF(msg, ...) HT_LOGF(Logger::Priority::DEBUG, msg, __VA_ARGS__)
#define HT_DEBUG_OUT HT_OUT2(Logger::Priority::DEBUG)
#else
#define HT_LOG_ENTER
#define HT_LOG_EXIT
#define HT_DEBUG(msg)
#define HT_DEBUGF(msg, ...)
#define HT_DEBUG_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_INFO
#define HT_INFO(msg) HT_LOG(Logger::Priority::INFO, msg)
#define HT_INFOF(msg, ...) HT_LOGF(Logger::Priority::INFO, msg, __VA_ARGS__)
#else
#define HT_INFO(msg)
#define HT_INFOF(msg, ...)
#endif

#ifndef HT_DISABLE_LOG_NOTICE
#define HT_NOTICE(msg) HT_LOG(Logger::Priority::NOTICE, msg)
#define HT_NOTICEF(msg, ...) HT_LOGF(Logger::Priority::NOTICE, msg, __VA_ARGS__)
#define HT_NOTICE_OUT HT_OUT(Logger::Priority::NOTICE)
#else
#define HT_NOTICE(msg)
#define HT_NOTICEF(msg, ...)
#define HT_NOTICE_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_WARN
#define HT_WARN(msg) HT_LOG(Logger::Priority::WARN, msg)
#define HT_WARNF(msg, ...) HT_LOGF(Logger::Priority::WARN, msg, __VA_ARGS__)
#define HT_WARN_OUT HT_OUT2(Logger::Priority::WARN)
#else
#define HT_WARN(msg)
#define HT_WARNF(msg, ...)
#define HT_WARN_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_ERROR
#define HT_ERROR(msg) HT_LOG(Logger::Priority::ERROR, msg)
#define HT_ERRORF(msg, ...) HT_LOGF(Logger::Priority::ERROR, msg, __VA_ARGS__)
#define HT_ERROR_OUT HT_OUT2(Logger::Priority::ERROR)
#else
#define HT_ERROR(msg)
#define HT_ERRORF(msg, ...)
#define HT_ERROR_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_CRIT
#define HT_CRIT(msg) HT_LOG(Logger::Priority::CRIT, msg)
#define HT_CRITF(msg, ...) HT_LOGF(Logger::Priority::CRIT, msg, __VA_ARGS__)
#define HT_CRIT_OUT HT_OUT2(Logger::Priority::CRIT)
#else
#define HT_CRIT(msg)
#define HT_CRITF(msg, ...)
#define HT_CRIT_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_ALERT
#define HT_ALERT(msg) HT_LOG(Logger::Priority::ALERT, msg)
#define HT_ALERTF(msg, ...) HT_LOGF(Logger::Priority::ALERT, msg, __VA_ARGS__)
#define HT_ALERT_OUT HT_OUT2(Logger::Priority::ALERT)
#else
#define HT_ALERT(msg)
#define HT_ALERTF(msg, ...)
#define HT_ALERT_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_EMERG
#define HT_EMERG(msg) HT_LOG(Logger::Priority::EMERG, msg)
#define HT_EMERGF(msg, ...) HT_LOGF(Logger::Priority::EMERG, msg, __VA_ARGS__)
#define HT_EMERG_OUT HT_OUT2(Logger::Priority::EMERG)
#else
#define HT_EMERG(msg)
#define HT_EMERGF(msg, ...)
#define HT_EMERG_OUT HT_OUT_DISABLED
#endif

#ifndef HT_DISABLE_LOG_FATAL
#define HT_FATAL(msg) do { \
  HT_LOG(Logger::Priority::FATAL, msg); \
  HT_ABORT; \
} while (0)
#define HT_FATALF(msg, ...) do { \
  HT_LOGF(Logger::Priority::FATAL, msg, __VA_ARGS__); \
  HT_ABORT; \
} while (0)
#define HT_FATAL_OUT HT_OUT2(Logger::Priority::FATAL)
#else
#define HT_FATAL(msg)
#define HT_FATALF(msg, ...)
#define HT_FATAL_OUT HT_OUT_DISABLED
#endif

#else // HT_DISABLE_LOGGING

#define HT_DEBUG(msg)
#define HT_DEBUGF(msg, ...)
#define HT_INFO(msg)
#define HT_INFOF(msg, ...)
#define HT_NOTICE(msg)
#define HT_NOTICEF(msg, ...)
#define HT_WARN(msg)
#define HT_WARNF(msg, ...)
#define HT_ERROR(msg)
#define HT_ERRORF(msg, ...)
#define HT_CRIT(msg)
#define HT_CRITF(msg, ...)
#define HT_ALERT(msg)
#define HT_ALERTF(msg, ...)
#define HT_EMERG(msg)
#define HT_EMERGF(msg, ...)
#define HT_FATAL(msg)
#define HT_FATALF(msg, ...)
#define HT_LOG_ENTER
#define HT_LOG_EXIT
#define HT_DEBUG_OUT HT_OUT_DISABLED
#define HT_NOTICE_OUT HT_OUT_DISABLED
#define HT_WARN_OUT HT_OUT_DISABLED
#define HT_ERROR_OUT HT_OUT_DISABLED
#define HT_CRIT_OUT HT_OUT_DISABLED
#define HT_ALERT_OUT HT_OUT_DISABLED
#define HT_EMERG_OUT HT_OUT_DISABLED
#define HT_FATAL_OUT HT_OUT_DISABLED

#endif // HT_DISABLE_LOGGING

// Probably should be in its own file, but...
#define HT_EXPECT(_e_, _code_) do { if (_e_); else { \
    if (_code_ == Error::FAILED_EXPECTATION) \
      HT_FATAL("failed expectation: " #_e_); \
    HT_THROW(_code_, "failed expectation: " #_e_); } \
} while (0)

// A short cut for HT_EXPECT(expr, Error::FAILED_EXPECTATION)
// unlike assert, it cannot be turned off
#define HT_ASSERT(_e_) HT_EXPECT(_e_, Error::FAILED_EXPECTATION)

#endif // Common_Logger_h
