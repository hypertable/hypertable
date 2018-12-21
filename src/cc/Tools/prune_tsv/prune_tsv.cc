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

#include <Common/Compat.h>

#include <AsyncComm/Config.h>

#include <Common/Init.h>
#include <Common/Logger.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace std;

namespace {

  const char *usage =
    "\nusage: prune_tsv [options] <past-date-offset>\n\n"
    "description:\n"
    "  This program removes lines read from stdin that contain a timestamp\n"
    "  or date string representing a date that is older than the current time\n"
    "  minus <past-date-offset>.  The <past-date-offset> argument can be specified\n"
    "  as days, months, or years (examples:  1y, 6m, 21d).  The --field option\n"
    "  can be used to select the tab delimted field to search (default == 0).  If\n"
    "  the field contains all digits, then it is interpreted as nanoseconds since\n"
    "  the epoch (or seconds if --seconds option is supplied).  Otherwise, the\n"
    "  field is searched for the pattern YYYY-MM-DD, which is taken to be the date.\n\n"
    "options";

  struct AppPolicy : Policy {
    static void init_options() {
      cmdline_desc(usage).add_options()
        ("field", i32(0), "Field number of each line to parse")
        ("newer", boo(false)->zero_token(), "Remove lines that are newer than calculated cutoff date")
        ("seconds", boo(false)->zero_token(), "Interpret all-digit fields as seconds instead of nanoseconds")
        ("zhack", boo(false)->zero_token(), "")
        ;
      cmdline_hidden_desc().add_options()
      ("past-date-offset", str(), "")
      ("past-date-offset", -1);
    }
    static void init() {
      if (!has("past-date-offset")) {
	cout << cmdline_desc() << endl;
	quick_exit(EXIT_FAILURE);
      }
    }
  };

  typedef Meta::list<AppPolicy, DefaultCommPolicy> Policies;

  inline char *get_field(char *line, int fieldno, char **endptr) {
    char *ptr, *base = line;

    ptr = strchr(base, '\t');
    while (fieldno && ptr) {
      fieldno--;
      base = ptr+1;
      ptr = strchr(base, '\t');
    }

    if (fieldno > 0)
      return 0;

    if (ptr) {
      *ptr = 0;
      *endptr = ptr;
    }
    return base;
  }

  inline const char *find_date(const char *str, bool *formattedp) {
    const char *slash = str;
    bool alldigits = true;

    while (*slash != '-' && *slash != '\t' && *slash != '\n' && *slash) {
      if (!isdigit(*slash))
        alldigits = false;
      slash++;
    }

    if (*slash != '-') {
      if (alldigits && slash > str) {
        *formattedp = false;
        return str;
      }
      return 0;
    }

    do {
      if (slash - str > 4 &&
	  isdigit(*(slash-4)) &&
	  isdigit(*(slash-3)) &&
	  isdigit(*(slash-2)) &&
	  isdigit(*(slash-1)) &&
	  isdigit(*(slash+1)) &&
	  isdigit(*(slash+2)) &&
	  *(slash+3) == '-' &&
	  isdigit(*(slash+4)) &&
	  isdigit(*(slash+5))) {
        *formattedp = true;
	return slash - 4;
      }
    } while ((slash = strchr(slash+1, '-')) != 0);

    return 0;
  }

  inline time_t find_seconds(const char *str) {
    const char *period = strchr(str, '.');

    if (period == 0)
      return 0;

    const char *ptr = period;
    while (ptr > str && isdigit(*(ptr-1)))
      ptr--;

    return strtol(ptr, 0, 10);
  }

  time_t parse_date_offset(const char *str) {
    time_t date_offset = 0;
    char *end;
    long n = strtol(str, &end, 10);

    if (end == str || *end == '\0') {
      cout << "\nERROR: Invalid <past-date-offset> argument: " << str << "\n" << endl;
      quick_exit(EXIT_FAILURE);
    }

    if (*end == 'd' || *end == 'D')
      date_offset = n * 24 * 60 * 60;
    else if (*end == 'm' || *end == 'M')
      date_offset = n * 30 * 24 * 60 * 60;
    else if (*end == 'y' || *end == 'Y')
      date_offset = n * 365 * 24 * 60 * 60;
    else {
      cout << "\nERROR: Invalid <past-date-offset> argument: " << str << "\n" << endl;
      quick_exit(EXIT_FAILURE);
    }
    return date_offset;
  }


}

/**
 *
 */
int main(int argc, char **argv) {
  string date_offset_str;
  const char *base;
  char *end = 0;
  time_t date_offset, cutoff_time, line_time;
  struct tm tm;
  char cutoff[32];
  int32_t field;
  bool newer = false;
  bool seconds = false;
  bool formatted;

  char *line_buffer = new char [ 1024 * 1024 ];

  ios::sync_with_stdio(false);

  try {
    init_with_policies<Policies>(argc, argv);

    field = get_i32("field");

    date_offset_str = get_str("past-date-offset");
    date_offset = parse_date_offset(date_offset_str.c_str());
    newer = get_bool("newer");
    seconds = get_bool("seconds");

    cutoff_time = time(0) - date_offset;

    if (get_bool("zhack")) {
      time_t line_seconds;
      while (!cin.eof()) {

        cin.getline(line_buffer, 1024*1024);
	if (cin.eof() && cin.gcount() <= 1)
	  break;
        if ((base = get_field(line_buffer, field, &end)) &&
            (line_seconds = find_seconds(base)) &&
            ((!newer && line_seconds < cutoff_time) ||
	     (newer && line_seconds > cutoff_time)))
          continue;
        if (end)
          *end = '\t';
        cout << line_buffer << "\n";
      }
    }
    else {
      localtime_r(&cutoff_time, &tm);
      strftime(cutoff, sizeof(cutoff), "%F", &tm);
      while (!cin.eof()) {
        cin.getline(line_buffer, 1024*1024);
        if (line_buffer[0] == 0)
          continue;
        if ((base = get_field(line_buffer, field, &end)) &&
            (base = find_date(base, &formatted))) {
          if (formatted) {
            if ((!newer && memcmp(base, cutoff, 10) < 0) ||
                (newer && memcmp(base, cutoff, 10) >= 0))
              continue;
          }
          else {
            if (seconds)
              line_time = (time_t)strtoll(base, &end, 10);
            else
              line_time = (time_t)(strtoll(base, &end, 10) / 1000000000LL);
            if ((!newer && (line_time < cutoff_time)) ||
                (newer && (line_time >= cutoff_time)))
              continue;
          }
        }
        if (end)
          *end = '\t';
        cout << line_buffer << "\n";
      }
    }
    cout << flush;
    quick_exit(EXIT_SUCCESS);
  }
  catch (std::exception &e) {
    cerr << "Error - " << e.what() << endl;
    exit(EXIT_FAILURE);
  }

}
