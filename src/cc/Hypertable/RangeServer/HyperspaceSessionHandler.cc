/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Common/Compat.h"
#include "HyperspaceSessionHandler.h"

using namespace Hyperspace;

/**
 *
 */
void HyperspaceSessionHandler::safe() {
  HT_INFO("Hyperspace session state change:  SAFE");
}

/**
 *
 */
void HyperspaceSessionHandler::jeopardy() {
  HT_INFO("Hyperspace session state change:  JEOPARDY");
}

/**
 *
 */
void HyperspaceSessionHandler::expired() {
  HT_ERROR_OUT << "Hyperspace session expired.  Exiting..." << HT_END;
  _exit(-1);
}

/**
 *
 */
void HyperspaceSessionHandler::disconnected() {
  HT_INFO("Hyperspace session state:  DISCONNECTED...");
}

/**
 *
 */
void HyperspaceSessionHandler::reconnected() {
  HT_INFO("Hyperspace session state:  RECONNECTED...");
}
