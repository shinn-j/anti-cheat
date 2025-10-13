#include "log.hpp"

// Helpful build metadata for logs
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
  // Defer to the Windows-specific implementation
  return RunWindowsAgent();
#else
  // Non-Windows platforms build & run, but skip Windows-only features
  Log::write(LogLevel::Info, "Windows-only features disabled on this platform.");
  Log::write(LogLevel::Info, "Agent exiting normally.");
  return 0;
#endif
}