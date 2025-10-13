#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include "log.hpp"

// --- Helpers ---
static DWORD FindPidByExe(const std::wstring& exe) {
  PROCESSENTRY32W pe{ sizeof(pe) };
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  DWORD pid = 0;
  if (Process32FirstW(snap, &pe)) {
    do { if (exe == pe.szExeFile) { pid = pe.th32ProcessID; break; } }
    while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return pid;
}

static std::wstring QueryExePath(HANDLE hProc) {
  wchar_t buf[MAX_PATH];
  DWORD sz = MAX_PATH;
  if (QueryFullProcessImageNameW(hProc, 0, buf, &sz)) return std::wstring(buf, sz);
  return L"";
}

static uint64_t FNV1a64(const void* data, size_t len) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint64_t h = 1469598103934665603ull;
  for (size_t i=0;i<len;++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}

static uint64_t HashWholeFile(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return 0;
  std::vector<char> buf(1<<20); // 1MB
  uint64_t h = 1469598103934665603ull;
  while (f) {
    f.read(buf.data(), buf.size());
    std::streamsize got = f.gcount();
    if (got > 0) h = FNV1a64(buf.data(), static_cast<size_t>(got)) ^ (h + 0x9e3779b97f4a7c15ull);
  }
  return h;
}

static void ListModules(HANDLE hProc) {
  HMODULE mods[1024]; DWORD needed=0;
  if (!EnumProcessModules(hProc, mods, sizeof(mods), &needed)) return;
  size_t count = needed/sizeof(HMODULE);
  for (size_t i=0;i<count;++i) {
    wchar_t name[MAX_PATH]; if (GetModuleFileNameExW(hProc, mods[i], name, MAX_PATH)) {
      Log::write(LogLevel::Info, "Module: %ls", name);
    }
  }
}

// --- Entry ---
int RunWindowsAgent() {
  const std::wstring target = L"GameHarness.exe";
  DWORD pid = FindPidByExe(target);
  if (!pid) { Log::write(LogLevel::Warn, "GameHarness not running"); return 0; }

  Log::write(LogLevel::Info, "Found GameHarness pid=%u", (unsigned)pid);
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) { Log::write(LogLevel::Error, "OpenProcess failed (%lu)", GetLastError()); return 1; }

  std::wstring exePath = QueryExePath(h);
  if (exePath.empty()) { Log::write(LogLevel::Warn, "Could not query exe path"); }
  else {
    Log::write(LogLevel::Info, "Exe: %ls", exePath.c_str());
    uint64_t hval = HashWholeFile(exePath);
    Log::write(LogLevel::Info, "Whole-file hash (FNV-1a mix): 0x%016llx", (unsigned long long)hval);
  }

  Log::write(LogLevel::Info, "Listing modules...");
  ListModules(h);

  CloseHandle(h);
  Log::write(LogLevel::Info, "Agent done");
  return 0;
}
#endif
