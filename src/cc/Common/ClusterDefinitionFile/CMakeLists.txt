#
# Copyright (C) 2007-2016 Hypertable, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#

set(SRC_DIR "${HYPERTABLE_SOURCE_DIR}/src/cc/Common/ClusterDefinitionFile")
set(DST_DIR "${HYPERTABLE_BINARY_DIR}/src/cc/Common/ClusterDefinitionFile")

set(Translator_test_SRCS
TokenizerTools.cc
TranslatorCode.cc
TranslatorRole.cc
TranslatorTask.cc
TranslatorVariable.cc
)
set(Tokenizer_test_SRCS
Token.cc
Tokenizer.cc
${Translator_test_SRCS}
)

# Tokenizer_test
HT_ADD_TEST(
	NAME Cluster-definition-tokenizer
	SRCS ${Tokenizer_test_SRCS} tests/Tokenizer_test.cc
	TARGETS Hypertable 
	ARGS ${SRC_DIR}/tests/Tokenizer_test.input ${SRC_DIR}/tests/Tokenizer_test.golden
)

# Translator test
HT_ADD_TEST(
	NAME Cluster-definition-translator
	SRCS ${Translator_test_SRCS} tests/Translator_test.cc
	TARGETS Hypertable 
	ARGS ${SRC_DIR}/tests/Translator_test.golden
)
