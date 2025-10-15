#pragma once
#include <cstddef>
#include <string>

struct EvalRow {
  std::size_t tick{};
  double speed{};
  double ml_prob{};     // probability from model (0..1). 0 if not using ML
  int ml_pred{};        // 0/1 after threshold + optional hybrid + debounce
  int rule_alert{};     // 0/1 from rule-based detector (your robust/abs OR)
  int cheat_flag{};     // ground truth from CSV
};

inline std::string evalHeader() {
  return "tick,speed,ml_prob,ml_pred,rule_alert,cheat_flag\n";
}

inline std::string evalToCsv(const EvalRow& r) {
  // integers printed without decimals for cleanliness
  return std::to_string(r.tick) + "," +
         std::to_string(r.speed) + "," +
         std::to_string(r.ml_prob) + "," +
         std::to_string(r.ml_pred) + "," +
         std::to_string(r.rule_alert) + "," +
         std::to_string(r.cheat_flag) + "\n";
}
