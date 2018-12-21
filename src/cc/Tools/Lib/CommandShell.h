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

#ifndef Tools_Lib_CommandShell_h
#define Tools_Lib_CommandShell_h

#include "CommandInterpreter.h"
#include "Notifier.h"

#include <Common/Config.h>

#include <histedit.h>

#include <memory>
#include <string>

namespace Hypertable {

  class CommandShell {
  public:
    CommandShell(const std::string &prompt_str, const std::string &service_name,
                 CommandInterpreterPtr &, PropertiesPtr &);

    ~CommandShell();

    int run();

    bool silent() { return m_silent; }
    bool test_mode() { return m_test_mode; }
    void set_namespace(const String &ns) { m_namespace=ns; }
    void set_line_command_mode(bool val) { m_line_command_mode=val; }

    static void add_options(PropertiesDesc &);

    static String ms_history_file;

  private:
    char *rl_gets();
    static CommandShell *ms_instance;
    static const wchar_t *prompt(EditLine *el);

    CommandInterpreterPtr m_interp_ptr;
    PropertiesPtr m_props;
    NotifierPtr m_notifier_ptr;

    String m_accum;
    bool m_verbose {};
    bool m_batch_mode {};
    bool m_silent {};
    bool m_test_mode {};
    bool m_no_prompt {};
    bool m_line_command_mode {};
    bool m_cont {};
    char *m_line_read {};
    bool m_notify {};
    bool m_has_cmd_file {};
    bool m_has_cmd_exec {};
    String m_input_str;
    std::string m_prompt;
    std::wstring m_wprompt;
    std::string m_service_name;
    String m_cmd_str;
    String m_cmd_file;
    String m_namespace;

    EditLine *m_editline;
    HistoryW *m_history;
    HistEventW m_history_event;
    TokenizerW *m_tokenizer;
  };

  typedef std::shared_ptr<CommandShell> CommandShellPtr;

  struct CommandShellPolicy : Config::Policy {
    static void init_options() {
      CommandShell::add_options(Config::cmdline_desc());
    }
  };


}

#endif // Tools_Lib_CommandShell_h

