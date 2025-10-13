#include "log.hpp"


#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm> // nth_element, max_element
#include <cstdlib>


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
int RunWindowsAgent(); // implemented in win_agent.cpp on Windows
#endif

// Telemetry row struct
struct TelemetryRow {
  double t{}, x{}, y{}, vx{}, vy{};
  int action{}, ping{}, cheat{};
};

// Helperse
static double median_inplace(std::vector<double> v) {
  if (v.empty()) return 0.0;
  size_t mid = v.size()/2;
  std::nth_element(v.begin(), v.begin()+mid, v.end());
  double m = v[mid];
  if (v.size() % 2 == 0) {
    // For even count, also find max of lower half and average (simple, good enough)
    auto lower_max = *std::max_element(v.begin(), v.begin()+mid);
    m = 0.5 * (m + lower_max);
  }
  return m;
}

static double mad_inplace(const std::vector<double>& v, double med) {
  if (v.empty()) return 0.0;
  std::vector<double> dev; dev.reserve(v.size());
  for (double x : v) dev.push_back(std::abs(x - med));
  size_t mid = dev.size()/2;
  std::nth_element(dev.begin(), dev.begin()+mid, dev.end());
  double m = dev[mid];
  if (dev.size() % 2 == 0) {
    auto lower_max = *std::max_element(dev.begin(), dev.begin()+mid);
    m = 0.5 * (m + lower_max);
  }
  return m;
}

// Main detector (reads CSV, computes thresholds, flags alerts, prints eval)
static void detectAnomalies() {
  // --- 1) Load data ---
  std::ifstream f("data/telemetry.csv");
  if (!f.is_open()) {
    Log::write(LogLevel::Warn, "No data/telemetry.csv found. Run game_harness first.");
    return;
  }

  std::string line;
  if (!std::getline(f, line)) {
    Log::write(LogLevel::Warn, "Empty telemetry.csv");
    return;
  }

  std::vector<TelemetryRow> rows;
  std::vector<double> speeds; speeds.reserve(1024);

  while (std::getline(f, line)) {
    // Parse a CSV row: timestamp,x,y,vx,vy,action,ping_ms,cheat_flag
    std::stringstream ss(line);
    TelemetryRow r; char c;
    ss >> r.t >> c >> r.x >> c >> r.y >> c >> r.vx >> c >> r.vy >> c
       >> r.action >> c >> r.ping >> c >> r.cheat;
    rows.push_back(r);
    speeds.push_back(std::sqrt(r.vx*r.vx + r.vy*r.vy));
  }
  if (rows.empty()) {
    Log::write(LogLevel::Warn, "No data rows in telemetry.csv");
    return;
  }

  // --- 2) Mean & stddev (population) ---
  double sum = 0.0;
  for (double s : speeds) sum += s;
  double mean = sum / speeds.size();

  double sqsum = 0.0;
  for (double s : speeds) sqsum += (s - mean) * (s - mean);
  double stddev = std::sqrt(sqsum / speeds.size());

  // Threshold from global mean/stddev
  double thr_3sigma = mean + 3.0 * stddev;

  // --- 3) Robust thresholds (median & MAD) ---
  double med = median_inplace(speeds);
  double MAD = mad_inplace(speeds, med);
  // 1.4826 makes MAD comparable to stddev for normal data
  double thr_robust = med + 3.0 * 1.4826 * MAD;

  // --- 4) Domain absolute cap (simple rule) ---
  double thr_abs = 6.0; // tweakable

  Log::write(LogLevel::Info,
    "Thresholds: mean=%.3f std=%.3f thr3σ=%.3f | median=%.3f MAD=%.3f thrRobust=%.3f | abs=%.2f",
    mean, stddev, thr_3sigma, med, MAD, thr_robust, thr_abs);

  // Open alerts.csv (optional)
  std::ofstream alerts("data/alerts.csv", std::ios::trunc);
  const bool writeAlerts = alerts.is_open();
  if (writeAlerts) {
    alerts << "tick,speed,r3,robust,abs,thr3,thrRobust,thrAbs\n";
  } else {
    Log::write(LogLevel::Warn, "Could not open data/alerts.csv (missing data dir?) — continuing without CSV output");
  }

  // --- 5) Scan rows: raise alerts & compute evaluation ---
  int TP=0, FP=0, FN=0, TN=0;
  int alertsCount = 0;

  for (size_t i = 0; i < rows.size(); ++i) {
    double speed = speeds[i];

    // Rule flags
    bool r3  = (speed > thr_3sigma);
    bool rrb = (speed > thr_robust);
    bool rab = (speed > thr_abs);

    // Liberal combo (OR). For conservative behavior, use: ((r3 && rrb) || rab)
    bool is_alert = r3 || rrb || rab;

    bool trueCheat = (rows[i].cheat == 1);

    if (is_alert) {
      ++alertsCount;
      Log::write(LogLevel::Warn,
        "ALERT: tick=%zu speed=%.2f | r3=%d robust=%d abs=%d (thr3=%.2f thrR=%.2f abs=%.2f)",
        i, speed, (int)r3, (int)rrb, (int)rab, thr_3sigma, thr_robust, thr_abs);

      if (writeAlerts) {
        alerts << i << ',' << speed << ','
               << (int)r3 << ',' << (int)rrb << ',' << (int)rab << ','
               << thr_3sigma << ',' << thr_robust << ',' << thr_abs << '\n';
      }
    }

    // Confusion matrix
    if (is_alert && trueCheat) ++TP;
    else if (is_alert && !trueCheat) ++FP;
    else if (!is_alert && trueCheat) ++FN;
    else ++TN;
  }

  double precision = (TP+FP) ? (double)TP / (TP+FP) : 0.0;
  double recall    = (TP+FN) ? (double)TP / (TP+FN) : 0.0;

  Log::write(LogLevel::Info, "Detected %d anomalies out of %zu rows", alertsCount, rows.size());
  Log::write(LogLevel::Info, "Eval: TP=%d FP=%d FN=%d TN=%d | precision=%.2f recall=%.2f",
             TP, FP, FN, TN, precision, recall);
}

// Main entrypoint (unchanged structure)
int main() {
  Log::init();
  Log::write(LogLevel::Info, "Agent starting | platform=%s | build=%s %s",
             AC_PLATFORM, __DATE__, __TIME__);

#ifdef _WIN32
  return RunWindowsAgent();
#else
  // Non-Windows: run rule-based detector over telemetry CSV
  detectAnomalies();
  Log::write(LogLevel::Info, "Windows-only features disabled on this platform.");
  Log::write(LogLevel::Info, "Agent exiting normally.");
  return 0;
#endif
}
