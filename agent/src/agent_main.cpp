#include "log.hpp"

// ---- Summarizer (non-Windows build) ----
#include <fstream>
#include <string>

static void summarizeTelemetry() {
  std::ifstream f("data/telemetry.csv");
  if (!f.is_open()) {
    Log::write(LogLevel::Warn, "No data/telemetry.csv found (run game_harness first?)");
    return;
  }

  std::string line;
  // skip header
  if (!std::getline(f, line)) {
    Log::write(LogLevel::Warn, "Empty data/telemetry.csv");
    return;
  }

  int rows = 0, cheats = 0;
  while (std::getline(f, line)) {
    ++rows;
    // quick parse: look at last field after final comma
    auto pos = line.find_last_of(',');
    if (pos != std::string::npos && pos + 1 < line.size()) {
      if (line[pos + 1] == '1') ++cheats;
    }
  }
  Log::write(LogLevel::Info, "Telemetry summary: rows=%d, cheat_rows=%d", rows, cheats);
}

// ---- Helpful build metadata for logs ----
#if defined(_WIN32)
  #define AC_PLATFORM "windows"
#elif defined(__APPLE__)
  #define AC_PLATFORM "macos"
#elif defined(__linux__)
  #define AC_PLATFORM "linux"
#else
  #define AC_PLATFORM "unknown"
#endif

#ifdef _WIN32
// Implemented in win_agent.cpp (Windows-only)
int RunWindowsAgent();
#endif

int main() {
  Log::init();
  Log::write(LogLevel::Info, "Agent starting | platform=%s | build=%s %s",
             AC_PLATFORM, __DATE__, __TIME__);

#ifdef _WIN32
  return RunWindowsAgent();
#else
  // On macOS/Linux, summarize the CSV produced by game_harness
  summarizeTelemetry();
  Log::write(LogLevel::Info, "Windows-only features disabled on this platform.");
  Log::write(LogLevel::Info, "Agent exiting normally.");
  return 0;
#endif
}
