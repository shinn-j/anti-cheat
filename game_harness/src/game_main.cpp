#include "log.hpp"
#include <thread>
#include <chrono>

int main() {
  Log::init();
  Log::write(LogLevel::Info, "GameHarness starting");
  for (int i = 0; i < 10; ++i) {
    Log::write(LogLevel::Info, "tick %d", i);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ~20 Hz
  }
  Log::write(LogLevel::Info, "GameHarness exiting");
  return 0;
}
