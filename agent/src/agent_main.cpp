#include "log.hpp"
int main() {
  Log::init();
  Log::write(LogLevel::Info, "Agent starting (stub)");
  return 0;
}
