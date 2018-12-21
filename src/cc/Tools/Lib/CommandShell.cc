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

#include <Common/Compat.h>

#include "CommandShell.h"

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/System.h>
#include <Common/Usage.h>
#include <Common/Logger.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/thread/exceptions.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <thread>

extern "C" {
#include <dirent.h>
#include <editline/readline.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
}

using namespace Hypertable;
using namespace std;

String CommandShell::ms_history_file = "";

namespace {
  const char *help_text =
  "\n" \
  "Interpreter Meta Commands\n" \
  "-------------------------\n" \
  "?          (\\?) Synonym for `help'.\n" \
  "clear      (\\c) Clear command.\n" \
  "exit [rc]  (\\q) Exit program with optional return code rc.\n" \
  "print      (\\p) Print current command.\n" \
  "quit       (\\q) Quit program.\n" \
  "source <f> (.)  Execute commands in file <f>.\n" \
  "system     (\\!) Execute a system shell command.\n" \
  "\n";

  char *find_char(const char *s, int c) {
    bool in_quotes = false;
    char quote_char = 0;

    for (const char *ptr = s; *ptr; ptr++) {
      if (in_quotes) {
        if (*ptr == quote_char && *(ptr - 1) != '\\')
          in_quotes = false;
      }
      else {
        if (*ptr == (char )c)
          return (char *)ptr;
        else if (*ptr == '\'' || *ptr == '"') {
          in_quotes = true;
          quote_char = *ptr;
        }
      }
    }
    return 0;
  }

  const string longest_common_prefix(vector<string> &completions) {
    if (completions.empty())
      return "";
    else if (completions.size() == 1)
      return completions[0];

    char ch, memo;
    size_t idx = 0;
    while (true) {
      memo = '\0';
      size_t i;
      for (i=0; i<completions.size(); i++) {
        if (completions[i].length() == idx)
          break;
        ch = completions[i].at(idx);
        if (memo == '\0')
          memo = ch;
        else if (ch != memo)
          break;
      }
      if (i < completions.size())
        return completions[0].substr(0, idx);
      idx++;
    }
    return "";
  }

  unsigned char complete(EditLine *el, int ch) {
    struct dirent *dp;
    const wchar_t *ptr;
    char *buf, *bptr;
    const LineInfoW *lf = el_wline(el);
    int len, mblen, i;
    unsigned char res = 0;
    wchar_t dir[1024];

    /* Find the last word */
    for (ptr = lf->cursor -1; !iswspace(*ptr) && ptr > lf->buffer; --ptr)
      continue;
    if (ptr > lf->buffer)
      ptr++;
    len = lf->cursor - ptr;

    /* Convert last word to multibyte encoding, so we can compare to it */
    wctomb(NULL, 0); /* Reset shift state */
    mblen = MB_LEN_MAX * len + 1;
    buf = bptr = (char *)malloc(mblen);
    for (i = 0; i < len; ++i) {
      /* Note: really should test for -1 return from wctomb */
      bptr += wctomb(bptr, ptr[i]);
    }
    *bptr = 0; /* Terminate multibyte string */
    mblen = bptr - buf;

    string directory;
    string prefix;

    bptr = strrchr(buf, '/');
    if (bptr == nullptr) {
      directory.append(".");
      prefix.append(buf);
    }
    else if (bptr == buf) {
      directory.append("/");
      prefix.append(buf+1);
    }
    else if (buf[0] == '~') {
      directory.append(buf, bptr-buf);
      FileUtils::expand_tilde(directory);
      prefix.append(bptr+1);
    }
    else {
      if (buf[0] != '/')
        directory.append("./");
      directory.append(buf, bptr-buf);
      prefix.append(bptr+1);
    }

    vector<string> completions;

    DIR *dd = opendir(directory.c_str());
    if (dd) {
      string completion;
      for (dp = readdir(dd); dp != NULL; dp = readdir(dd)) {
        if (strncmp(dp->d_name, prefix.c_str(), prefix.length()) == 0) {
          completion = string(&dp->d_name[prefix.length()]);
          if (dp->d_type == DT_DIR)
            completion.append("/");
          completions.push_back(completion);
        }
      }
      string longest_prefix = longest_common_prefix(completions);
      if (!longest_prefix.empty()) {
        mbstowcs(dir, longest_prefix.c_str(), sizeof(dir) / sizeof(*dir));
        if (el_winsertstr(el, dir) == -1)
          res = CC_ERROR;
        else
          res = CC_REFRESH;
      }
      closedir(dd);
    }

    free(buf);
    return res;
  }

}

CommandShell *CommandShell::ms_instance;

/**
 */
CommandShell::CommandShell(const string &prompt_str, const string &service_name,
                           CommandInterpreterPtr &interp_ptr,
                           PropertiesPtr &props)
  : m_interp_ptr(interp_ptr), m_props(props), m_prompt(prompt_str),
    m_service_name(service_name) {

  const char *home = getenv("HOME");
  if (home)
    ms_history_file = (String)home + "/." + m_prompt + "_history";
  else
    ms_history_file = (String)"." + m_prompt + "_history";

  m_verbose = m_props->has("verbose") ? (bool)m_props->get<gBool>("verbose") : false;
  m_batch_mode = m_props->has("batch");
  m_silent = m_props->has("silent") && m_props->get_bool("silent");
  m_test_mode = m_props->has("test-mode");
  if (m_test_mode) {
    Logger::get()->set_test_mode();
    m_batch_mode = true;
  }
  m_no_prompt = m_props->has("no-prompt");

  m_notify = m_props->has("notification-address");
  if (m_notify) {
    String notification_address = m_props->get_str("notification-address");
    m_notifier_ptr = make_shared<Notifier>(notification_address.c_str());
  }

  if (m_props->has("execute")) {
    m_cmd_str = m_props->get_str("execute");
    boost::trim(m_cmd_str);
    if (!m_cmd_str.empty() && m_cmd_str[m_cmd_str.length()] != ';')
      m_cmd_str.append(";");
    m_has_cmd_exec = true;
    m_batch_mode = true;
  }
  else if (m_props->has("command-file")) {
    m_cmd_file = m_props->get_str("command-file");
    m_has_cmd_file = true;
    m_batch_mode = true;
  }

  setlocale(LC_ALL, "");

  /* initialize libedit */
  if (!m_batch_mode) {
    ms_instance = this;
    m_editline = el_init("hypertable", stdin, stdout, stderr);
    m_history = history_winit();
    history_w(m_history, &m_history_event, H_SETSIZE, 100);
    history_w(m_history, &m_history_event, H_LOAD, ms_history_file.c_str());

    el_wset(m_editline, EL_HIST, history_w, m_history);
    el_wset(m_editline, EL_PROMPT, prompt);
    el_wset(m_editline, EL_SIGNAL, 1);
    el_wset(m_editline, EL_EDITOR, L"emacs");

    /* Add a user-defined function	*/
    el_wset(m_editline, EL_ADDFN, L"ed-complete", L"Complete argument", complete);

    /* Bind <tab> to it */
    el_wset(m_editline, EL_BIND, L"^I", L"ed-complete", NULL);

    /* Source the user's defaults file. */
    el_source(m_editline, NULL);
    m_tokenizer = tok_winit(NULL);
  }
  else {
    m_editline = 0;
    m_history = 0;
    m_tokenizer = 0;
  }

  // Initialize prompt string
  wchar_t buf[64] = {0};
  const char *p = prompt_str.c_str();
  mbsrtowcs(buf, &p, 63, 0);
  m_wprompt = buf;
  m_wprompt += L"> ";

  // Propagate mode flags to interpreter
  m_interp_ptr->set_interactive_mode(!m_batch_mode);
  m_interp_ptr->set_silent(m_silent);
}

CommandShell::~CommandShell() {
  if (m_editline)
    el_end(m_editline);
  if (m_history)
    history_wend(m_history);
  if (m_tokenizer)
    tok_wend(m_tokenizer);
}

/**
 */
char *CommandShell::rl_gets () {
  if (m_line_read) {
    free(m_line_read);
    m_line_read = (char *)NULL;
  }

  /* Execute commands from command line string/file */
  if (m_has_cmd_exec || m_has_cmd_file) {
    static bool done = false;

    if (done)
      return 0;

    if (m_has_cmd_exec) {
      m_line_read = (char *)malloc(m_cmd_str.size() + 1);
      strcpy(m_line_read, m_cmd_str.c_str());
    }
    else {
      off_t len;
      char *tmp;
      // copy bcos readline uses malloc, FileUtils::file_to_buffer uses new
      tmp = FileUtils::file_to_buffer(m_cmd_file, &len);
      m_line_read = (char *)malloc(len+1);
      memcpy(m_line_read, tmp, len);
      m_line_read[len] = 0;
      delete[] tmp;
    }

    done = true;
    return m_line_read;
  }

  /* Get a line from the user. */
  if (m_batch_mode || m_no_prompt || m_silent) {
    if (!getline(cin, m_input_str))
      return 0;
    boost::trim(m_input_str);
    if (m_input_str.find("quit", 0) != 0 && m_test_mode)
      cout << m_input_str << endl;
    return (char *)m_input_str.c_str();
  }
  else {
    const wchar_t *wline = 0;
    int numc = 0;
    while (true) {
      wline = el_wgets(m_editline, &numc);
      if (wline == 0 || numc == 0)
        return (char *)"exit";

      if (!m_cont && numc == 1)
        continue;  /* Only got a linefeed */

      const LineInfoW *li = el_wline(m_editline);
      int ac = 0, cc = 0, co = 0;
      const wchar_t **av;
      int ncont = tok_wline(m_tokenizer, li, &ac, &av, &cc, &co);
      if (ncont < 0) {
        // (void) fprintf(stderr, "Internal error\n");
        m_cont = false;
        continue;
      }

      if (el_wparse(m_editline, ac, av) == -1)
        break;
    }

    char *buffer = (char *)malloc(1024 * 8);
    size_t len = 1024 * 8;
    while (1) {
      const wchar_t *wp = &wline[0];
      size_t l = wcsrtombs(buffer, &wp, len, 0);
      if (l > len) {
        buffer = (char *)realloc(buffer, l + 1);
        len = l + 1;
      }
      else
        break;
    }
    m_line_read = buffer;

    /* If the line has any text in it, save it on the history. */
    if (!m_batch_mode && m_line_read && *m_line_read)
      history_w(m_history, &m_history_event,
              m_cont ? H_APPEND : H_ENTER, wline);
  }

  return m_line_read;
}

void CommandShell::add_options(PropertiesDesc &desc) {
  desc.add_options()
    ("batch", "Disable interactive behavior")
    ("no-prompt", "Do not display an input prompt")
    ("test-mode", "Don't display anything that might change from run to run "
        "(e.g. timing statistics)")
    ("timestamp-format", str(), "Output format for timestamp. "
        "Currently the only formats are 'default' and 'nanoseconds'")
    ("notification-address", str(), "[<host>:]<port> "
        "Send notification datagram to this address after each command.")
    ("execute,exec,e", str(), "Execute specified commands.")
    ("command-file", str(), "Execute commands from file.")
    ;
}


int CommandShell::run() {
  const char *line;
  std::queue<string> command_queue;
  String command;
  String timestamp_format;
  String source_commands;
  String use_ns;
  const char *base, *ptr;
  int exit_status = 0;

  if (m_props->has("timestamp-format"))
    timestamp_format = m_props->get_str("timestamp-format");

  if (timestamp_format != "")
    m_interp_ptr->set_timestamp_output_format(timestamp_format);

  if (!m_batch_mode && !m_silent && !m_batch_mode) {
    read_history(ms_history_file.c_str());

    cout << endl;
    cout << "Welcome to the " << m_prompt << " command interpreter."
         << endl;
    cout << "For information about Hypertable, visit http://hypertable.com"
         << endl;
    cout << endl;
    cout << "Type 'help' for a list of commands, or 'help shell' for a" << endl;
    cout << "list of shell meta commands." << endl;
    cout << endl << flush;
  }

  m_accum = "";
  if (!m_batch_mode)
    using_history();

  if (!m_prompt.compare("hypertable")) {
    trim_if(m_namespace, boost::is_any_of(" \t\n\r;"));
    if (m_namespace.empty())
      m_namespace = "/";
    use_ns = "USE \"" + m_namespace + "\";";
    line = use_ns.c_str();
    goto process_line;
  }

  while ((line = rl_gets()) != 0) {
process_line:
    try {
      if (*line == 0)
        continue;

      if (!strncasecmp(line, "help shell", 10)) {
        cout << help_text;
        continue;
      }
      else if (!strncasecmp(line, "help", 4)
               || !strncmp(line, "\\h", 2) || *line == '?') {
        command = line;
        std::transform(command.begin(), command.end(), command.begin(),
                       ::tolower);
        trim_if(command, boost::is_any_of(" \t\n\r;"));
        m_interp_ptr->execute_line(command);
        if (m_notify)
          m_notifier_ptr->notify();
        continue;
      }
      else if (!strncasecmp(line, "quit", 4) || !strcmp(line, "\\q")) {
        if (!m_batch_mode)
          history_w(m_history, &m_history_event, H_SAVE,
                  ms_history_file.c_str());
        return exit_status;
      }
      else if (!strncasecmp(line, "exit", 4)) {
        if (!m_batch_mode)
          history_w(m_history, &m_history_event, H_SAVE,
                  ms_history_file.c_str());
        const char *ptr = line + 4;
        while (*ptr && isspace(*ptr))
          ptr++;
        return (*ptr) ? atoi(ptr) : exit_status;
      }
      else if (!strncasecmp(line, "print", 5) || !strcmp(line, "\\p")) {
        cout << m_accum << endl;
        continue;
      }
      else if (!strncasecmp(line, "clear", 5) || !strcmp(line, "\\c")) {
        m_accum = "";
        m_cont = false;
        continue;
      }
      else if (!strncasecmp(line, "source", 6) || line[0] == '.') {
        if ((base = strchr(line, ' ')) == 0) {
          cout << "syntax error: source or '.' must be followed by a space "
              "character" << endl;
          continue;
        }
        String fname = base;
        trim_if(fname, boost::is_any_of(" \t\n\r;"));
        off_t flen;
        if ((base = FileUtils::file_to_buffer(fname.c_str(), &flen)) == 0)
          continue;
        source_commands = "";
        ptr = strtok((char *)base, "\n\r");
        while (ptr != 0) {
          command = ptr;
          boost::trim(command);
          if (command.find("#") != 0)
            source_commands += command + " ";
          ptr = strtok(0, "\n\r");
        }
        if (source_commands == "")
          continue;
        delete [] base;
        line = source_commands.c_str();
      }
      else if (!strncasecmp(line, "system", 6) || !strncmp(line, "\\!", 2)) {
        String command = line;
        size_t offset = command.find_first_of(' ');
        if (offset != String::npos) {
          command = command.substr(offset+1);
          trim_if(command, boost::is_any_of(" \t\n\r;"));
          HT_EXPECT(system(command.c_str()) == 0, Error::EXTERNAL);
        }
        continue;
      }

      /**
       * Add commands to queue
       */
      if (m_line_command_mode) {
        if (*line == 0 || *line == '#')
          continue;
        command_queue.push(line);
      }
      else {
        base = line;
        ptr = find_char(base, ';');
        while (ptr) {
          m_accum += string(base, ptr-base);
          if (m_accum.size() > 0) {
            boost::trim(m_accum);
            if (m_accum.find("#") != 0)
              command_queue.push(m_accum);
            m_accum = "";
            m_cont = false;
          }
          base = ptr+1;
          ptr = find_char(base, ';');
        }
        command = string(base);
        boost::trim(command);
        if (command != "" && command.find("#") != 0) {
          m_accum += command;
          boost::trim(m_accum);
        }
        if (m_accum != "") {
          m_cont = true;
          m_accum += " ";
        }
      }

      while (!command_queue.empty()) {
        if (command_queue.front() == "quit") {
          if (!m_batch_mode)
            history_w(m_history, &m_history_event, H_SAVE,
                    ms_history_file.c_str());
          return exit_status;
        }
        else if (boost::algorithm::starts_with(command_queue.front(), "exit")) {
          if (!m_batch_mode)
            history_w(m_history, &m_history_event, H_SAVE,
                    ms_history_file.c_str());
          const char *ptr = command_queue.front().c_str() + 4;
          while (*ptr && isspace(*ptr))
            ptr++;
          return (*ptr) ? atoi(ptr) : exit_status;
        }
        command = command_queue.front();
        command_queue.pop();
        if (!strncmp(command.c_str(), "pause", 5)) {
          String sec_str = command.substr(5);
          boost::trim(sec_str);
          char *endptr;
          long secs = strtol(sec_str.c_str(), &endptr, 0);
          if ((secs == 0 && errno == EINVAL) || *endptr != 0) {
            cout << "error: invalid seconds specification" << endl;
            if (m_batch_mode)
              return 2;
          }
          else
            this_thread::sleep_for(chrono::milliseconds(secs*1000));
        }
        else {
          exit_status = m_interp_ptr->execute_line(command);
          if(m_notify)
            m_notifier_ptr->notify();
        }
      }
    }
    catch (Hypertable::Exception &e) {
      if (e.code() == Error::BAD_NAMESPACE)
        cerr << "ERROR: No namespace is open (see 'use' command)" << endl;
      else {
        if (m_verbose)
          cerr << e << endl;
        else if (!m_silent)
          cout << m_service_name << " CRITICAL - " << Error::get_text(e.code())
               << " (" << e.what() << ")" << endl;
      }
      if(m_notify)
        m_notifier_ptr->notify();
      exit_status = 2;
      if (m_batch_mode && !m_test_mode)
        return exit_status;
      m_accum = "";
      while (!command_queue.empty())
        command_queue.pop();
      m_cont = false;
    }
  }

  if (m_batch_mode) {
    boost::trim(m_accum);
    if (!m_accum.empty()) {
      line = ";";
      goto process_line;
    }
  }

  if (!m_batch_mode)
    history_w(m_history, &m_history_event, H_SAVE, ms_history_file.c_str());

  return exit_status;
}

const wchar_t *
CommandShell::prompt(EditLine *el) {
  if (ms_instance->m_cont)
    return L"         -> ";
  else
    return ms_instance->m_wprompt.c_str();
}
