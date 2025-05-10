// Copyright (C) [2025] [@kleedaisuki] <kleedaisuki@outlook.com>
// This file is part of Simple-K Cloud Executor.
//
// Simple-K Cloud Executor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Simple-K Cloud Executor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Simple-K Cloud Executor.  If not, see <https://www.gnu.org/licenses/>.

#ifndef _CLOUD_COMPILE_BACKEND_HPP
#define _CLOUD_COMPILE_BACKEND_HPP

#include "network/network.hpp"
#include "frontend-defs.hpp"

#ifndef _GLOBAL_CONSTANT_CPP
#endif

void make_sure_log_file(void) throws(runtime_error);
void close_log_file(void) throws(runtime_error);

int runMainWindow(ClientSocket &client, const vector<string> &args);

#endif
