#include "log.hpp"
#include "model_loader.hpp"
#include "eval_io.hpp"
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

struct AgentConfig {
  bool quiet = false;                   // suppress per-tick logs
  bool hybrid = std::getenv("AC_HYBRID") != nullptr;   // default: env
  std::string eval_path = "data/eval.csv";
};

// trivial flag parser: --quiet  --no-hybrid  --eval=PATH
static AgentConfig parseArgs(int argc, char** argv) {
  AgentConfig cfg;
  for (int i=1; i<argc; ++i) {
    std::string a = argv[i];
    if (a == "--quiet") cfg.quiet = true;
    else if (a == "--hybrid") cfg.hybrid = true;
    else if (a == "--no-hybrid") cfg.hybrid = false;
    else if (a.rfind("--eval=",0)==0) cfg.eval_path = a.substr(7);
  }
  return cfg;
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

static void detectCheatsML(const AgentConfig& CFG) {
  // --- 0) Open telemetry.csv ---
  std::filesystem::create_directories("data"); // ensure data dir exists

  std::ifstream f("data/telemetry.csv");
  if (!f.is_open()) {
    Log::write(LogLevel::Warn, "No data/telemetry.csv found. Run game_harness first.");
    return;
  }

  try {
    auto sz = std::filesystem::file_size("data/telemetry.csv");
    const std::uintmax_t MAX_BYTES = 10 * 1024 * 1024; // 10 MB
    if (sz > MAX_BYTES) {
      Log::write(LogLevel::Warn,
                "telemetry.csv too large (%ju bytes) — truncating read",
                static_cast<uintmax_t>(sz));
    }
  } catch (const std::filesystem::filesystem_error& e) {
    Log::write(LogLevel::Warn, "Could not check file size: %s", e.what());
  }


  // --- 1) Read header ---
  std::string line;
  if (!std::getline(f, line)) {
    Log::write(LogLevel::Warn, "Empty telemetry.csv");
    return;
  }

  // --- 2) Read all rows into memory ---
  std::vector<TelemetryRow> rows;
  rows.reserve(1024);
  while (std::getline(f, line)) {
    TelemetryRow r; char c; std::stringstream ss(line);
    // CSV: timestamp,x,y,vx,vy,action,ping_ms,cheat_flag
    ss >> r.t >> c >> r.x >> c >> r.y >> c >> r.vx >> c >> r.vy
       >> c >> r.action >> c >> r.ping >> c >> r.cheat;
    rows.push_back(r);
  }
  if (rows.empty()) {
    Log::write(LogLevel::Warn, "No data rows in telemetry.csv");
    return;
  }

  // --- 3) Load model ---
  const std::string modelPath = resolveModelPath();
  Model model = loadModel(modelPath);
  Log::write(LogLevel::Info, "Loaded ML model from '%s' with %zu features, threshold=%.2f",
             modelPath.c_str(), model.features.size(), model.threshold);

  // --- 4) Compute robust thresholds for the hybrid gate ---
  std::vector<double> speeds; speeds.reserve(rows.size());
  for (auto& r : rows) speeds.push_back(std::sqrt(r.vx*r.vx + r.vy*r.vy));

  // median (in-place) helper you already have
  double med = median_inplace(speeds);
  double MAD = mad_inplace(speeds, med);
  double thr_robust = med + 3.0 * 1.4826 * MAD;
  double thr_abs    = 6.0; // domain cap you’ve been using

  Log::write(LogLevel::Info,
    "ML pass thresholds (hybrid gates): median=%.3f MAD=%.3f thrRobust=%.3f | abs=%.2f",
    med, MAD, thr_robust, thr_abs);

  // --- 5) Inference with rolling features ---
  const size_t W = 5;
  std::deque<double> w;
  auto roll_mean = [&](){
    if (w.empty()) return 0.0;
    double s = std::accumulate(w.begin(), w.end(), 0.0);
    return s / static_cast<double>(w.size());
  };
  auto roll_std = [&](){
    if (w.size() <= 1) return 0.0;
    double m = roll_mean();
    double acc = 0.0;
    for (double v : w) acc += (v - m) * (v - m);
    return std::sqrt(acc / static_cast<double>(w.size()));
  };

  bool have_prev = false;
  double prev_vx = 0.0, prev_vy = 0.0;

  std::vector<EvalRow> eval; eval.reserve(rows.size());
  size_t tick = 0;

  for (const auto& r : rows) {
    const double speed = std::sqrt(r.vx*r.vx + r.vy*r.vy);

    // rolling window over speed
    w.push_back(speed);
    if (w.size() > W) w.pop_front();
    const double smean = roll_mean();
    const double sstd  = roll_std();

    // Δv -> accel magnitude
    double ax = 0.0, ay = 0.0;
    if (have_prev) { ax = r.vx - prev_vx; ay = r.vy - prev_vy; }
    const double accel_mag = std::sqrt(ax*ax + ay*ay);
    prev_vx = r.vx; prev_vy = r.vy; have_prev = true;

    // features: ["speed","accel_mag","speed_roll_mean","speed_roll_std","ping_ms","action"]
    std::vector<double> feats = {
      speed, accel_mag, smean, sstd,
      static_cast<double>(r.ping), static_cast<double>(r.action)
    };

    const double prob = inferCheatProbability(model, feats);
    const bool ml_alert  = (prob >= model.threshold);
    const bool rule_gate = (speed > thr_robust) || (speed > thr_abs);
    const bool pred = CFG.hybrid ? (ml_alert && rule_gate) : ml_alert;

    if (pred && !CFG.quiet) {
      Log::write(LogLevel::Warn, "AI ALERT: tick=%zu prob=%.3f%s",
                 tick, prob, CFG.hybrid ? " (hybrid)" : "");
    }

    eval.push_back(EvalRow{
      tick, speed, prob, pred ? 1 : 0, rule_gate ? 1 : 0, (r.cheat == 1) ? 1 : 0
    });

    ++tick;
  }

  // --- 6) Metrics from eval ---
  int TP=0, FP=0, FN=0, TN=0;
  int alerts=0;
  for (const auto& e : eval) {
    bool pred  = (e.ml_pred == 1);
    bool truth = (e.cheat_flag == 1);
    if (pred) ++alerts;
    if (pred && truth) ++TP;
    else if (pred && !truth) ++FP;
    else if (!pred && truth) ++FN;
    else ++TN;
  }
  double precision = (TP+FP) ? (double)TP/(TP+FP) : 0.0;
  double recall    = (TP+FN) ? (double)TP/(TP+FN) : 0.0;

  Log::write(LogLevel::Info, "AI detected %d potential cheats", alerts);
  Log::write(LogLevel::Info, "AI Eval: TP=%d FP=%d FN=%d TN=%d | precision=%.2f recall=%.2f",
             TP, FP, FN, TN, precision, recall);

  // --- 7) Write eval.csv ---
  std::ofstream ef(CFG.eval_path, std::ios::trunc);
  if (!ef.is_open()) {
    Log::write(LogLevel::Warn, "Could not open %s for writing", CFG.eval_path.c_str());
  } else {
    ef << evalHeader();
    for (const auto& r : eval) ef << evalToCsv(r);
    Log::write(LogLevel::Info, "Wrote %zu eval rows to %s", eval.size(), CFG.eval_path.c_str());
  }
}




// Rule-based detector
static void detectAnomalies() {
  std::ifstream f("data/telemetry.csv");
  if (!f.is_open()) {
    Log::write(LogLevel::Warn, "No data/telemetry.csv found. Run game_harness first.");
    return;
  }

  try {
    auto sz = std::filesystem::file_size("data/telemetry.csv");
    const std::uintmax_t MAX_BYTES = 10 * 1024 * 1024; // 10 MB
    if (sz > MAX_BYTES) {
      Log::write(LogLevel::Warn,
                "telemetry.csv too large (%ju bytes) — truncating read",
                static_cast<uintmax_t>(sz));
    }
  } catch (const std::filesystem::filesystem_error& e) {
    Log::write(LogLevel::Warn, "Could not check file size: %s", e.what());
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

int main(int argc, char** argv) {
  Log::init();
  AgentConfig CFG = parseArgs(argc, argv);
  Log::write(LogLevel::Info, "Agent starting | platform=%s | build=%s %s",
             AC_PLATFORM, __DATE__, __TIME__);
#ifndef _WIN32
  detectAnomalies();      // optional, rule pass
  detectCheatsML(CFG);    // ML pass with CSV export
#endif
}

