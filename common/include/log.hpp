#pragma once
#include <string>
enum class LogLevel { Info, Warn, Error };
namespace Log {
  void init(const std::string& dir = "./logs");
  void write(LogLevel lvl, const char* fmt, ...);
}
