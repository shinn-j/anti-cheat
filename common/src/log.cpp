#include "log.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdarg>
#include <cstdio>
#include <iostream>

static std::mutex g_m;
static std::ofstream g_file;

void Log::init(const std::string& dir) {
  std::scoped_lock lk(g_m);
  std::filesystem::create_directories(dir);
  g_file.open(dir + "/log.txt", std::ios::app);
}

void Log::write(LogLevel lvl, const char* fmt, ...) {
  std::scoped_lock lk(g_m);
  const char* tag = (lvl==LogLevel::Info)?"I":(lvl==LogLevel::Warn)?"W":"E";
  char buf[1024];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::cout << "[" << tag << "] " << buf << "\n";
  if (g_file.is_open()) g_file << "[" << tag << "] " << buf << "\n";
}
