#include "log.hpp"
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <filesystem>

struct PlayerState {
  double x{0}, y{0};
  double vx{0}, vy{0};
  int action{0};
  int ping_ms{50};
};

int main() {
  Log::init();
  Log::write(LogLevel::Info, "GameHarness telemetry run starting");

  std::filesystem::create_directories("data");
  std::ofstream csv("data/telemetry.csv");
  if (!csv.is_open()) {
    Log::write(LogLevel::Error, "Failed to open data/telemetry.csv for writing");
    return 1;
  }
  csv << "timestamp,x,y,vx,vy,action,ping_ms,cheat_flag\n";

  std::mt19937 rng{42};
  std::normal_distribution<double> move(0.0, 1.0);
  std::uniform_int_distribution<int> act(0, 3);
  std::uniform_int_distribution<int> ping(30, 70);

  PlayerState p;
  const auto start = std::chrono::steady_clock::now();

  for (int tick = 0; tick < 100; ++tick) {
    auto now = std::chrono::steady_clock::now();
    double t = std::chrono::duration<double>(now - start).count();

    p.vx = move(rng);
    p.vy = move(rng);
    p.x += p.vx;
    p.y += p.vy;
    p.action = act(rng);
    p.ping_ms = ping(rng);

    int cheat = (tick >= 70 && tick <= 80) ? 1 : 0;
    if (cheat) { p.vx *= 5; p.vy *= 5; }

    csv << t << ',' << p.x << ',' << p.y << ',' << p.vx << ',' << p.vy << ','
        << p.action << ',' << p.ping_ms << ',' << cheat << '\n';

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  Log::write(LogLevel::Info, "Telemetry complete -> data/telemetry.csv");
}
