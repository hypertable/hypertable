/* -*- c++ -*-
 * Copyright (C) 2007-2016 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#ifndef HYPERTABLE_HSPARSER_H
#define HYPERTABLE_HSPARSER_H

//#define BOOST_SPIRIT_DEBUG  ///$$$ DEFINE THIS WHEN DEBUGGING $$$///

#ifdef BOOST_SPIRIT_DEBUG
#define HS_DEBUG(str) std::cerr << str << std::endl
#else
#define HS_DEBUG(str)
#endif

#include <Hyperspace/Protocol.h>
#include <Hyperspace/Session.h>

#include <Common/Error.h>
#include <Common/FileUtils.h>
#include <Common/Logger.h>

#include <boost/algorithm/string.hpp>
#include <boost/spirit/include/classic_confix.hpp>
#include <boost/spirit/include/classic_core.hpp>
#include <boost/spirit/include/classic_escape_char.hpp>
#include <boost/spirit/include/classic_grammar.hpp>
#include <boost/spirit/include/classic_symbols.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace Hyperspace {

  namespace HsParser {

    using namespace boost;
    using namespace boost::spirit;
    using namespace boost::spirit::classic;

    enum {
      COMMAND_HELP=1,
      COMMAND_MKDIR,
      COMMAND_DELETE,
      COMMAND_OPEN,
      COMMAND_CREATE,
      COMMAND_CLOSE,
      COMMAND_ATTRSET,
      COMMAND_ATTRGET,
      COMMAND_ATTREXISTS,
      COMMAND_ATTRLIST,
      COMMAND_ATTRDEL,
      COMMAND_EXISTS,
      COMMAND_READDIR,
      COMMAND_LOCK,
      COMMAND_TRYLOCK,
      COMMAND_RELEASE,
      COMMAND_GETSEQ,
      COMMAND_ECHO,
      COMMAND_QUIT,
      COMMAND_LOCATE,
      COMMAND_READDIRATTR,
      COMMAND_ATTRINCR,
      COMMAND_READPATHATTR,
      COMMAND_DUMP,
      COMMAND_MKDIRS,
      COMMAND_STATUS,
      COMMAND_MAX,
      COMMAND_CFG_RELOAD
    };

    enum {
      LOCATE_MASTER=1,
      LOCATE_REPLICAS
    };

    class ParserState {
    public:
      ParserState() : open_flag(0), event_mask(0), command(0),
          lock_mode(0), last_attr_size(0), locate_type(0), recursive(false), as_commands(false) { }
      String file_name;
      String dir_name;
      String node_name;
      String last_attr_name;
      String last_attr_value;
      String str;
      String help_str;
      String outfile;
      std::vector<Attribute> attrs;
      std::unordered_map<String,String> attr_map;
      int open_flag;
      int event_mask;
      int command;
      int lock_mode;
      int last_attr_size;
      int locate_type;
      bool recursive;
      bool as_commands;
    };

    struct set_command {
      set_command(ParserState &state_, int cmd)
          : state(state_), command(cmd) { }
      void operator()(char const *, char const *) const {
        state.command = command;
      }
      ParserState &state;
      int command;
    };

    struct set_file_name {
      set_file_name(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.file_name = String(str, end-str);
      }
      ParserState &state;
    };

    struct set_as_commands {
      set_as_commands(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.as_commands = true;
      }
      ParserState &state;
    };

    struct set_outfile {
      set_outfile(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.outfile = String(str, end-str);
        trim_if(state.outfile, is_any_of("'\""));
      }
      ParserState &state;
    };

    struct set_dir_name {
      set_dir_name(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.dir_name = String(str, end-str);
      }
      ParserState &state;
    };

    struct set_node_name {
      set_node_name(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.node_name = String(str, end-str);
      }
      ParserState &state;
    };

    struct set_any_string {
      set_any_string(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.str = String(str, end-str);
      }
      ParserState &state;
    };

    struct set_help {
      set_help(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.command = COMMAND_HELP;
        state.help_str = String(str, end-str);
        trim(state.help_str);
        to_lower(state.help_str);
      }
      ParserState &state;
    };

    struct set_open_flag {
      set_open_flag(ParserState &state_, int open_flag_)
        : state(state_), open_flag(open_flag_) { }
      void operator()(char const *str, char const *end) const {
        state.open_flag |= open_flag;
      }
      ParserState &state;
      int open_flag;
    };

    struct set_event_mask {
      set_event_mask(ParserState &state_, int event_mask_)
        : state(state_), event_mask(event_mask_) { }
      void operator()(char const *str, char const *end) const {
        state.event_mask |= event_mask;
      }
      ParserState &state;
      int event_mask;
    };

    struct set_last_attr_name {
      set_last_attr_name(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        state.last_attr_name = String(str, end-str);
        HS_DEBUG("set_last_attr_name" << state.last_attr_name);
      }
      ParserState &state;
    };

    struct set_last_attr_value {
      set_last_attr_value(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        String escaped_double_quote("\\\"");
        String unescaped_double_quote(1, '"');
        String escaped_single_quote("\\\'");
        String unescaped_single_quote("'");

        state.last_attr_value = String(str, end-str);
        boost::trim_if(state.last_attr_value, is_any_of("'\""));
        boost::algorithm::replace_all(state.last_attr_value, escaped_double_quote, unescaped_double_quote);
        boost::algorithm::replace_all(state.last_attr_value, escaped_single_quote, unescaped_single_quote);

        HS_DEBUG("set_last_attr_value" << state.last_attr_value);
      }
      ParserState &state;
    };

    struct set_last_attr_size {
      set_last_attr_size(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        String size_str = String(str, end-str);
      }
      ParserState &state;
    };

    struct set_last_attr {
      set_last_attr(ParserState &state_) : state(state_) { }
      void operator()(char const *str, char const *end) const {
        Attribute attr;
        std::unordered_map<String,String>::iterator iter;

        state.attr_map[state.last_attr_name] = state.last_attr_value;
        iter = state.attr_map.find(state.last_attr_name);
        if(iter == state.attr_map.end())
          HT_THROW(Error::HYPERSPACE_CLI_PARSE_ERROR,
                   "Error: Problem setting attribute");

        attr.name = (*iter).first.c_str();
        attr.value = (*iter).second.c_str();
        attr.value_len = strlen((const char*) attr.value);
        state.attrs.push_back(attr);
        state.last_attr_size = attr.value_len;
      }
      ParserState &state;
    };

    struct set_lock_mode {
      set_lock_mode(ParserState &state_, int lock_mode_)
        : state(state_), lock_mode(lock_mode_) { }
      void operator()(char const *str, char const *end) const {
        state.lock_mode = lock_mode;
      }
      ParserState &state;
      int lock_mode;
    };

    struct set_locate_type {
      set_locate_type(ParserState &state_, int locate_type_)
        : state(state_), locate_type(locate_type_) { }
      void operator()(char const *str, char const *end) const {
        state.locate_type = locate_type;
      }
      ParserState &state;
      int locate_type;
    };

    struct set_recursive {
      set_recursive(ParserState &state) : state(state) { }
      void operator()(char const *str, char const *end) const {
       state.recursive = true;
      }
      ParserState &state;
    };

    struct Parser : public grammar<Parser> {
      Parser(ParserState &state_) : state(state_) { }

      template <typename Scanner>
      struct definition {

        definition(Parser const &self)  {
          keywords =
            "flags", "FLAGS", "attr", "ATTR", "event-mask", "EVENT-MASK";

          /*
           * OPERATORS
           */
          chlit<>     ONE('1');
          chlit<>     ZERO('0');
          chlit<>     SINGLEQUOTE('\'');
          chlit<>     DOUBLEQUOTE('"');
          chlit<>     QUESTIONMARK('?');
          chlit<>     PLUS('+');
          chlit<>     MINUS('-');
          chlit<>     STAR('*');
          chlit<>     SLASH('/');
          chlit<>     COMMA(',');
          chlit<>     SEMI(';');
          chlit<>     COLON(':');
          chlit<>     EQUAL('=');
          strlit<>    DOUBLEEQUAL("==");
          chlit<>     LT('<');
          strlit<>    LE("<=");
          strlit<>    GE(">=");
          chlit<>     GT('>');
          strlit<>    SW("=^");
          chlit<>     LPAREN('(');
          chlit<>     RPAREN(')');
          chlit<>     LBRACK('[');
          chlit<>     RBRACK(']');
          chlit<>     POINTER('^');
          chlit<>     DOT('.');
          chlit<>     BITOR('|');
          strlit<>    DOTDOT("..");
          strlit<>    DOUBLEQUESTIONMARK("??");

          /*
           * TOKENS
           */
          typedef inhibit_case<strlit<> > Token;
          Token C_MKDIR                = as_lower_d["mkdir"];
          Token C_MKDIRS               = as_lower_d["mkdirs"];
          Token C_DELETE               = as_lower_d["delete"];
          Token C_OPEN                 = as_lower_d["open"];
          Token C_CREATE               = as_lower_d["create"];
          Token C_CLOSE                = as_lower_d["close"];
          Token C_ATTRSET              = as_lower_d["attrset"];
          Token C_ATTRGET              = as_lower_d["attrget"];
          Token C_ATTRINCR             = as_lower_d["attrincr"];
          Token C_ATTRDEL              = as_lower_d["attrdel"];
          Token C_ATTREXISTS           = as_lower_d["attrexists"];
          Token C_ATTRLIST             = as_lower_d["attrlist"];
          Token C_EXISTS               = as_lower_d["exists"];
          Token C_READDIR              = as_lower_d["readdir"];
          Token C_READDIRATTR          = as_lower_d["readdirattr"];
          Token C_READPATHATTR         = as_lower_d["readpathattr"];
          Token C_DUMP                 = as_lower_d["dump"];
          Token C_LOCK                 = as_lower_d["lock"];
          Token C_TRYLOCK              = as_lower_d["trylock"];
          Token C_RELEASE              = as_lower_d["release"];
          Token C_GETSEQ               = as_lower_d["getseq"];
          Token C_ECHO                 = as_lower_d["echo"];
          Token C_HELP                 = as_lower_d["help"];
          Token C_LOCATE               = as_lower_d["locate"];
          Token C_STATUS               = as_lower_d["status"];
          Token C_CFG_RELOAD           = as_lower_d["cfg_reload"];

          Token ESC_HELP               = as_lower_d["\\h"];

          Token AS_COMMANDS          = as_lower_d["as_commands"];
          Token FLAGS                = as_lower_d["flags"];
          Token EVENTMASK            = as_lower_d["event-mask"];
          Token O_READ               = as_lower_d["read"];
          Token O_WRITE              = as_lower_d["write"];
          Token O_LOCK               = as_lower_d["lock"];
          Token O_CREATE             = as_lower_d["create"];
          Token O_EXCLUSIVE          = as_lower_d["excl"];
          Token O_TEMP               = as_lower_d["temp"];
          Token O_LOCK_SHARED        = as_lower_d["lock_shared"];
          Token O_LOCK_EXCLUSIVE     = as_lower_d["lock_exclusive"];
          Token M_ATTR_SET           = as_lower_d["attr_set"];
          Token M_ATTR_DEL           = as_lower_d["attr_del"];
          Token M_CHILD_NODE_ADDED   = as_lower_d["child_node_added"];
          Token M_CHILD_NODE_REMOVED = as_lower_d["child_node_removed"];
          Token M_LOCK_ACQUIRED      = as_lower_d["lock_acquired"];
          Token M_LOCK_RELEASED      = as_lower_d["lock_released"];
          Token ATTR                 = as_lower_d["attr"];
          Token L_SHARED             = as_lower_d["shared"];
          Token L_EXCLUSIVE          = as_lower_d["exclusive"];
          Token R_MASTER             = as_lower_d["master"];
          Token R_REPLICAS           = as_lower_d["replicas"];
          Token FLAG_R               = as_lower_d["-r"];

          /*
           * Start grammar definition
           */

          node_name
            = lexeme_d[(+(anychar_p - space_p)) - (keywords)];

          identifier
            = lexeme_d[(alpha_p >> *(alnum_p | '_')) - (keywords)];

          string_literal
            = single_string_literal
            | double_string_literal
            ;

          any_string
            = lexeme_d[*(anychar_p)]
            ;

          single_string_literal
            = confix_p(SINGLEQUOTE, *lex_escape_ch_p, SINGLEQUOTE);

          double_string_literal
            = confix_p(DOUBLEQUOTE, *lex_escape_ch_p, DOUBLEQUOTE);

          user_identifier
            = identifier
            | string_literal
            ;

          statement
            = mkdirs_statement[set_command(self.state, COMMAND_MKDIRS)]
            | mkdir_statement[set_command(self.state, COMMAND_MKDIR)]
            | delete_statement[set_command(self.state, COMMAND_DELETE)]
            | open_statement[set_command(self.state, COMMAND_OPEN)]
            | create_statement[set_command(self.state, COMMAND_CREATE)]
            | close_statement[set_command(self.state, COMMAND_CLOSE)]
            | help_statement[set_command(self.state,COMMAND_HELP)]
            | attrset_statement[set_command(self.state, COMMAND_ATTRSET)]
            | attrget_statement[set_command(self.state, COMMAND_ATTRGET)]
            | attrincr_statement[set_command(self.state, COMMAND_ATTRINCR)]
            | attrdel_statement[set_command(self.state, COMMAND_ATTRDEL)]
            | attrexists_statement[set_command(self.state, COMMAND_ATTREXISTS)]
            | attrlist_statement[set_command(self.state, COMMAND_ATTRLIST)]
            | exists_statement[set_command(self.state,COMMAND_EXISTS)]
            | readdir_statement[set_command(self.state, COMMAND_READDIR)]
            | readdirattr_statement[set_command(self.state, COMMAND_READDIRATTR)]
            | readpathattr_statement[set_command(self.state, COMMAND_READPATHATTR)]
            | dump_statement[set_command(self.state, COMMAND_DUMP)]
            | lock_statement[set_command(self.state, COMMAND_LOCK)]
            | trylock_statement[set_command(self.state, COMMAND_TRYLOCK)]
            | release_statement[set_command(self.state, COMMAND_RELEASE)]
            | getseq_statement[set_command(self.state, COMMAND_GETSEQ)]
            | echo_statement[set_command(self.state, COMMAND_ECHO)]
            | locate_statement[set_command(self.state, COMMAND_LOCATE)]
            | cfg_reload_statement[set_command(self.state, COMMAND_CFG_RELOAD)]
            | status_statement[set_command(self.state, COMMAND_STATUS)]
            ;

          mkdir_statement
            = C_MKDIR >> node_name[set_dir_name(self.state)]
            ;

          mkdirs_statement
            = C_MKDIRS >> node_name[set_dir_name(self.state)]
            ;

          delete_statement
            = C_DELETE >> node_name[set_node_name(self.state)]
            ;

          open_statement
            = C_OPEN >> node_name[set_node_name(self.state)]
            >> !(FLAGS >> EQUAL >> open_flag_value)
            >> !(EVENTMASK >> EQUAL >> open_event_mask_value)
            ;

          create_statement
            = C_CREATE >> node_name[set_file_name(self.state)]
            >> FLAGS >> EQUAL >> create_flag_value
            >> *(one_create_option)
            ;

          close_statement
            = C_CLOSE >> node_name[set_node_name(self.state)];

          attrset_statement
            = C_ATTRSET >>  node_name[set_node_name(self.state)] >> attribute;

          attrget_statement
            = C_ATTRGET >> node_name[set_node_name(self.state)]
            >> user_identifier[set_last_attr_name(self.state)]
            ;

          attrincr_statement
            = C_ATTRINCR >> node_name[set_node_name(self.state)]
            >> user_identifier[set_last_attr_name(self.state)]
            ;

          attrdel_statement
            = C_ATTRDEL >> node_name[set_node_name(self.state)]
            >> user_identifier[set_last_attr_name(self.state)]
            ;

          attrexists_statement
            = C_ATTREXISTS >> node_name[set_node_name(self.state)]
            >> user_identifier[set_last_attr_name(self.state)]
            ;

          attrlist_statement
            = C_ATTRLIST >> node_name[set_node_name(self.state)]
            ;

          exists_statement
            = C_EXISTS >> node_name[set_node_name(self.state)]
            ;

          readdir_statement
            = C_READDIR >> node_name[set_dir_name(self.state)];

          readdirattr_statement
            = C_READDIRATTR >> !(FLAG_R[set_recursive(self.state)])
            >> node_name[set_dir_name(self.state)]
            >> user_identifier[set_last_attr_name(self.state)]
            ;

          readpathattr_statement
            = C_READPATHATTR >> node_name[set_dir_name(self.state)]
            >> user_identifier[set_last_attr_name(self.state)]
            ;

          dump_statement
            = C_DUMP >> node_name[set_dir_name(self.state)]
            >> !(AS_COMMANDS[set_as_commands(self.state)])
            >> !(string_literal[set_outfile(self.state)])
            ;


          lock_statement
            = C_LOCK >> node_name[set_node_name(self.state)] >> lock_mode;

          trylock_statement
            = C_TRYLOCK >> node_name[set_node_name(self.state)] >> lock_mode;

          release_statement
            = C_RELEASE >> node_name[set_node_name(self.state)];

          getseq_statement
            = C_GETSEQ >>  node_name[set_node_name(self.state)];

          echo_statement
            = C_ECHO >> any_string[set_any_string(self.state)];
            ;

          help_statement
            = (C_HELP | ESC_HELP | QUESTIONMARK )
            >> any_string[set_help(self.state)];
            ;

          locate_statement
            = C_LOCATE >> locate_type
            ;
			
          cfg_reload_statement
            = C_CFG_RELOAD >> node_name[set_file_name(self.state)];
            ;

          status_statement
            = C_STATUS
            ;

          one_open_flag_value
            = O_READ[set_open_flag(self.state, OPEN_FLAG_READ)]
            | O_WRITE[set_open_flag(self.state, OPEN_FLAG_WRITE)]
            | O_CREATE[set_open_flag(self.state, OPEN_FLAG_CREATE)]
            | O_EXCLUSIVE[set_open_flag(self.state, OPEN_FLAG_EXCL)]
            | O_TEMP[set_open_flag(self.state, OPEN_FLAG_TEMP)]
            | O_LOCK_SHARED[set_open_flag(self.state, OPEN_FLAG_LOCK_SHARED)]
            | O_LOCK_EXCLUSIVE[set_open_flag(self.state,
                               OPEN_FLAG_LOCK_EXCLUSIVE)]
            | O_LOCK[set_open_flag(self.state, OPEN_FLAG_LOCK)]
            ;
          open_flag_value
            = one_open_flag_value >> *(BITOR >> one_open_flag_value);

          one_open_event_mask_value
            = M_ATTR_SET[set_event_mask(self.state, EVENT_MASK_ATTR_SET)]
            | M_ATTR_DEL[set_event_mask(self.state, EVENT_MASK_ATTR_DEL)]
            | M_CHILD_NODE_ADDED[set_event_mask(self.state,
                                 EVENT_MASK_CHILD_NODE_ADDED)]
            | M_CHILD_NODE_REMOVED[set_event_mask(self.state,
                                   EVENT_MASK_CHILD_NODE_REMOVED)]
            | M_LOCK_ACQUIRED[set_event_mask(self.state,
                              EVENT_MASK_LOCK_ACQUIRED)]
            | M_LOCK_RELEASED[set_event_mask(self.state,
                              EVENT_MASK_LOCK_RELEASED)]
            ;
          open_event_mask_value
            = one_open_event_mask_value
              >> *(BITOR >> one_open_event_mask_value);

          one_create_flag_value
            = O_READ[set_open_flag(self.state, OPEN_FLAG_READ)]
            | O_WRITE[set_open_flag(self.state, OPEN_FLAG_WRITE)]
            | O_LOCK[set_open_flag(self.state, OPEN_FLAG_LOCK)]
            | O_EXCLUSIVE[set_open_flag(self.state, OPEN_FLAG_EXCL)]
            | O_TEMP[set_open_flag(self.state, OPEN_FLAG_TEMP)]
            | O_LOCK_SHARED[set_open_flag(self.state, OPEN_FLAG_LOCK_SHARED)]
            | O_LOCK_EXCLUSIVE[set_open_flag(self.state,
                               OPEN_FLAG_LOCK_EXCLUSIVE)]
            ;
          create_flag_value
            = one_create_flag_value >> *(BITOR >> one_create_flag_value);

          one_create_event_mask_value
            = M_ATTR_SET[set_event_mask(self.state, EVENT_MASK_ATTR_SET)]
            | M_ATTR_DEL[set_event_mask(self.state, EVENT_MASK_ATTR_DEL)]
            | M_LOCK_ACQUIRED[set_event_mask(self.state,
                              EVENT_MASK_LOCK_ACQUIRED)]
            | M_LOCK_RELEASED[set_event_mask(self.state,
                              EVENT_MASK_LOCK_RELEASED)]
            ;
          create_event_mask_value
            = one_create_event_mask_value
              >> *(BITOR >> one_create_event_mask_value);

          one_create_option
            = (
                (ATTR >> COLON >> attribute)
              | (
                EVENTMASK >> EQUAL >> create_event_mask_value
              ));

          attribute
            = (user_identifier[set_last_attr_name(self.state)]
              >> EQUAL >> user_identifier[set_last_attr_value(self.state)]
              )[set_last_attr(self.state)]
            ;

          lock_mode
            = L_SHARED[set_lock_mode(self.state, LOCK_MODE_SHARED)]
            | L_EXCLUSIVE[set_lock_mode(self.state, LOCK_MODE_EXCLUSIVE)]
            ;

          locate_type
            = R_MASTER[set_locate_type(self.state, LOCATE_MASTER)]
            | R_REPLICAS[set_locate_type(self.state, LOCATE_REPLICAS)]
            ;

          /*
           * End grammar definition
           */

#ifdef BOOST_SPIRIT_DEBUG
          BOOST_SPIRIT_DEBUG_RULE(identifier);
          BOOST_SPIRIT_DEBUG_RULE(string_literal);
          BOOST_SPIRIT_DEBUG_RULE(any_string);
          BOOST_SPIRIT_DEBUG_RULE(single_string_literal);
          BOOST_SPIRIT_DEBUG_RULE(double_string_literal);
          BOOST_SPIRIT_DEBUG_RULE(user_identifier);
          BOOST_SPIRIT_DEBUG_RULE(statement);
          BOOST_SPIRIT_DEBUG_RULE(mkdir_statement);
          BOOST_SPIRIT_DEBUG_RULE(mkdirs_statement);
          BOOST_SPIRIT_DEBUG_RULE(delete_statement);
          BOOST_SPIRIT_DEBUG_RULE(open_statement);
          BOOST_SPIRIT_DEBUG_RULE(create_statement);
          BOOST_SPIRIT_DEBUG_RULE(close_statement);
          BOOST_SPIRIT_DEBUG_RULE(help_statement);
          BOOST_SPIRIT_DEBUG_RULE(locate_statement);
          BOOST_SPIRIT_DEBUG_RULE(status_statement);
          BOOST_SPIRIT_DEBUG_RULE(cfg_reload_statement);
          BOOST_SPIRIT_DEBUG_RULE(attrset_statement);
          BOOST_SPIRIT_DEBUG_RULE(attrget_statement);
          BOOST_SPIRIT_DEBUG_RULE(attrincr_statement);
          BOOST_SPIRIT_DEBUG_RULE(attrexists_statement);
          BOOST_SPIRIT_DEBUG_RULE(attrlist_statement);
          BOOST_SPIRIT_DEBUG_RULE(attrdel_statement);
          BOOST_SPIRIT_DEBUG_RULE(exists_statement);
          BOOST_SPIRIT_DEBUG_RULE(readdir_statement);
          BOOST_SPIRIT_DEBUG_RULE(readdirattr_statement);
          BOOST_SPIRIT_DEBUG_RULE(readpathattr_statement);
          BOOST_SPIRIT_DEBUG_RULE(dump_statement);
          BOOST_SPIRIT_DEBUG_RULE(lock_statement);
          BOOST_SPIRIT_DEBUG_RULE(trylock_statement);
          BOOST_SPIRIT_DEBUG_RULE(release_statement);
          BOOST_SPIRIT_DEBUG_RULE(getseq_statement);
          BOOST_SPIRIT_DEBUG_RULE(echo_statement);
          BOOST_SPIRIT_DEBUG_RULE(one_open_flag_value);
          BOOST_SPIRIT_DEBUG_RULE(open_flag_value);
          BOOST_SPIRIT_DEBUG_RULE(one_open_event_mask_value);
          BOOST_SPIRIT_DEBUG_RULE(open_event_mask_value);
          BOOST_SPIRIT_DEBUG_RULE(one_create_flag_value);
          BOOST_SPIRIT_DEBUG_RULE(create_flag_value);
          BOOST_SPIRIT_DEBUG_RULE(one_create_event_mask_value);
          BOOST_SPIRIT_DEBUG_RULE(create_event_mask_value);
          BOOST_SPIRIT_DEBUG_RULE(one_create_option);
          BOOST_SPIRIT_DEBUG_RULE(attribute);
          BOOST_SPIRIT_DEBUG_RULE(lock_mode);
          BOOST_SPIRIT_DEBUG_RULE(node_name);
          BOOST_SPIRIT_DEBUG_RULE(locate_type);
#endif
        }

        rule<Scanner> const&
        start() const { return statement; }

        symbols<> keywords;

        rule<Scanner> identifier, string_literal, any_string,
          single_string_literal, double_string_literal, user_identifier,
          statement, mkdir_statement, mkdirs_statement, delete_statement, open_statement,
          create_statement, close_statement, help_statement, locate_statement,
          attrset_statement, attrget_statement, attrincr_statement,
          attrexists_statement,  attrdel_statement,
          attrlist_statement, exists_statement, readdir_statement,
          readdirattr_statement, readpathattr_statement, dump_statement, lock_statement,
          trylock_statement, release_statement, getseq_statement, echo_statement,
          one_open_flag_value, open_flag_value, one_open_event_mask_value,
          open_event_mask_value, one_create_flag_value, create_flag_value,
          one_create_event_mask_value, create_event_mask_value,
          one_create_option, attribute, lock_mode, node_name, locate_type,
          status_statement, cfg_reload_statement;
        };

      ParserState &state;
    };
  }
}

#endif // HYPERTABLE_HSPARSER_H
