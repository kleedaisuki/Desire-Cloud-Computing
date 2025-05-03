#ifndef _CLOUD_COMPILE_BACKEND_HPP
#define _CLOUD_COMPILE_BACKEND_HPP

#include "network/network.hpp"
#include "frontend-defs.hpp"

#ifndef _GLOBAL_CONSTANT_CPP
#endif

void make_sure_log_file(void) throws(runtime_error);
void close_log_file(void) throws(runtime_error);

int runMainWindow(const vector<string> &parameters);

#endif
