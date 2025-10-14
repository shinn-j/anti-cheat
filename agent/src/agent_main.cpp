#include "log.hpp"
#include "model_loader.hpp"
#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <deque>
#include <numeric>


// Platform banner
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

// Telemetry row
struct TelemetryRow {
  double t{}, x{}, y{}, vx{}, vy{};
  int action{}, ping{}, cheat{};
};

// Helpers: robust stats 
static double median_inplace(std::vector<double> v) {
  if (v.empty()) return 0.0;
  size_t mid = v.size()/2;
  std::nth_element(v.begin(), v.begin()+mid, v.end());
  double m = v[mid];
  if (v.size() % 2 == 0) {
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

// Forward declaration so main() can call it
static void detectAnomalies();

// ML detector helpers
static std::string resolveModelPath() {
  const char* candidates[] = {
    "models/logreg_export.json",        // run from repo root
    "../models/logreg_export.json",     // run from build/agent/
    "../../models/logreg_export.json"   // extra fallback
  };
  for (auto p : candidates) {
    if (std::filesystem::exists(p)) return std::string(p);
  }
  return "models/logreg_export.json"; // default; loadModel will throw if missing
}

static void detectCheatsML() {
  const std::string modelPath = resolveModelPath();
  Model model = loadModel(modelPath);
  Log::write(LogLevel::Info, "Loaded ML model from '%s' with %zu features, threshold=%.2f",
             modelPath.c_str(), model.features.size(), model.threshold);

  std::ifstream f("data/telemetry.csv");
  if (!f.is_open()) {
    Log::write(LogLevel::Warn, "No telemetry.csv found");
    return;
  }

  std::string line;
  if (!std::getline(f, line)) { // header
    Log::write(LogLevel::Warn, "Empty telemetry.csv");
    return;
  }

  // Rolling window for speed (size 5)
  const size_t W = 5;
  std::deque<double> w;
  auto roll_mean = [&]() {
    if (w.empty()) return 0.0;
    double s = std::accumulate(w.begin(), w.end(), 0.0);
    return s / static_cast<double>(w.size());
  };
  auto roll_std = [&]() {
    if (w.size() <= 1) return 0.0;
    double m = roll_mean();
    double acc = 0.0;
    for (double v : w) acc += (v - m) * (v - m);
    return std::sqrt(acc / static_cast<double>(w.size()));
  };

  // Optional hybrid gate toggle (ML && rule)
  const bool HYBRID = std::getenv("AC_HYBRID") == nullptr ? true : true;
  // Precompute simple rule thresholds from the whole file if you want stronger gating.
  // For now we’ll compute them on-the-fly using a first pass in detectAnomalies().
  // (If detectAnomalies() already ran this process earlier in main(), that’s fine.)

  // Previous velocity for Δv
  bool have_prev = false;
  double prev_vx = 0.0, prev_vy = 0.0;

  size_t tick = 0;
  int alerts = 0;
  int TP=0, FP=0, FN=0, TN=0;

  // If you want a local rule gate, collect speeds to compute robust thresholds once:
  std::vector<double> all_speeds; all_speeds.reserve(2048);

  // First read all rows into memory (small files), then do two passes:
  std::vector<TelemetryRow> rows;
  rows.reserve(1024);

  // we already consumed the header above
  while (std::getline(f, line)) {
    TelemetryRow r; char c; std::stringstream ss(line);
    ss >> r.t >> c >> r.x >> c >> r.y >> c >> r.vx >> c >> r.vy
      >> c >> r.action >> c >> r.ping >> c >> r.cheat;
    rows.push_back(r);
  }


  // Compute robust thresholds for optional hybrid gate
  for (const auto& r : rows) {
    all_speeds.push_back(std::sqrt(r.vx*r.vx + r.vy*r.vy));
  }
  double thr_abs = 6.0;
  // median + MAD
  auto median_local = [](std::vector<double> v){
    if (v.empty()) return 0.0;
    size_t mid = v.size()/2;
    std::nth_element(v.begin(), v.begin()+mid, v.end());
    double m = v[mid];
    if (v.size() % 2 == 0) {
      auto lower_max = *std::max_element(v.begin(), v.begin()+mid);
      m = 0.5*(m + lower_max);
    }
    return m;
  };
  auto mad_local = [&](const std::vector<double>& v, double med){
    if (v.empty()) return 0.0;
    std::vector<double> dev; dev.reserve(v.size());
    for (double x: v) dev.push_back(std::abs(x - med));
    size_t mid = dev.size()/2;
    std::nth_element(dev.begin(), dev.begin()+mid, dev.end());
    double m = dev[mid];
    if (dev.size() % 2 == 0) {
      auto lower_max = *std::max_element(dev.begin(), dev.begin()+mid);
      m = 0.5*(m + lower_max);
    }
    return m;
  };
  double med = median_local(all_speeds);
  double MAD = mad_local(all_speeds, med);
  double thr_robust = med + 3.0 * 1.4826 * MAD;

  // Second pass: inference with correct rolling features
  for (const auto& r : rows) {
    const double speed = std::sqrt(r.vx*r.vx + r.vy*r.vy);

    // Update rolling window (push newest, drop oldest if needed)
    w.push_back(speed);
    if (w.size() > W) w.pop_front();   // <-- key: drop oldest only
    const double smean = roll_mean();
    const double sstd  = roll_std();

    // Δv acceleration
    double ax = 0.0, ay = 0.0;
    if (have_prev) {
      ax = r.vx - prev_vx;
      ay = r.vy - prev_vy;
    }
    const double accel_mag = std::sqrt(ax*ax + ay*ay);
    prev_vx = r.vx; prev_vy = r.vy; have_prev = true;

    // Feature order MUST match Python export
    std::vector<double> feats = {
      speed, accel_mag, smean, sstd,
      static_cast<double>(r.ping), static_cast<double>(r.action)
    };

    const double prob = inferCheatProbability(model, feats);
    const bool ml_alert = (prob >= model.threshold);
    const bool rule_gate = (speed > thr_robust) || (speed > thr_abs);
    const bool pred = HYBRID ? (ml_alert && rule_gate) : ml_alert;
    const bool truth = (r.cheat == 1);

    if (pred) {
      ++alerts;
      Log::write(LogLevel::Warn, "AI ALERT: tick=%zu prob=%.3f%s",
                 tick, prob, HYBRID ? " (hybrid)" : "");
    }

    if (pred && truth) ++TP;
    else if (pred && !truth) ++FP;
    else if (!pred && truth) ++FN;
    else ++TN;

    ++tick;
  }

  double precision = (TP+FP) ? (double)TP / (TP+FP) : 0.0;
  double recall    = (TP+FN) ? (double)TP / (TP+FN) : 0.0;
  Log::write(LogLevel::Info, "AI detected %d potential cheats", alerts);
  Log::write(LogLevel::Info, "AI Eval: TP=%d FP=%d FN=%d TN=%d | precision=%.2f recall=%.2f",
             TP, FP, FN, TN, precision, recall);
}



// Rule-based detector
static void detectAnomalies() {
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

  double sum = 0.0; for (double s : speeds) sum += s;
  double mean = sum / speeds.size();

  double sqsum = 0.0;
  for (double s : speeds) sqsum += (s - mean) * (s - mean);
  double stddev = std::sqrt(sqsum / speeds.size());

  double thr_3sigma = mean + 3.0 * stddev;

  double med = median_inplace(speeds);
  double MAD = mad_inplace(speeds, med);
  double thr_robust = med + 3.0 * 1.4826 * MAD;

  double thr_abs = 6.0;

  Log::write(LogLevel::Info,
    "Thresholds: mean=%.3f std=%.3f thr3σ=%.3f | median=%.3f MAD=%.3f thrRobust=%.3f | abs=%.2f",
    mean, stddev, thr_3sigma, med, MAD, thr_robust, thr_abs);

  std::filesystem::create_directories("data");
  std::ofstream alerts("data/alerts.csv", std::ios::trunc);
  const bool writeAlerts = alerts.is_open();
  if (writeAlerts) {
    alerts << "tick,speed,r3,robust,abs,thr3,thrRobust,thrAbs\n";
  }

  int TP=0, FP=0, FN=0, TN=0;
  int alertsCount = 0;

  for (size_t i = 0; i < rows.size(); ++i) {
    double speed = speeds[i];

    bool r3  = (speed > thr_3sigma);
    bool rrb = (speed > thr_robust);
    bool rab = (speed > thr_abs);
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

// main
int main() {
  Log::init();
  Log::write(LogLevel::Info, "Agent starting | platform=%s | build=%s %s",
             AC_PLATFORM, __DATE__, __TIME__);

#ifdef _WIN32
  return RunWindowsAgent();
#else
  detectAnomalies();   // rule-based
  detectCheatsML();    // ML-based
  Log::write(LogLevel::Info, "Windows-only features disabled on this platform.");
  Log::write(LogLevel::Info, "Agent exiting normally.");
  return 0;
#endif
}
