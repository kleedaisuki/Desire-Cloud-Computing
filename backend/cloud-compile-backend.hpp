#ifndef _CLOUD_COMPILE_BACKEND_HPP
#define _CLOUD_COMPILE_BACKEND_HPP

#include <iostream>
#include <list>
#include <fstream>
#include <filesystem>
#include <sys/wait.h>

#include "network/network.hpp"
#include "backend-defs.hpp"

using namespace std;


#ifndef _GLOBAL_CONSTANT_CPP
extern ThreadPool global_thread_pool;
extern unordered_map<string, function<ThreadStatCode(void *)>> main_thread_process;
extern mutex log_mutex;
extern fstream log_file;
extern bool main_thread_stop_flag;
#endif

void make_sure_log_file(void) throws(runtime_error);
void close_log_file(void) throws(runtime_error);

void initialize(void) throws(runtime_error);
void finalize(void) throws(runtime_error);

#endif
