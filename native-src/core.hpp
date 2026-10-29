#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <vector>
#include <array>
#include <string>
#include <map>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iomanip>
#include <cpuid.h>
#include <x86intrin.h>
namespace ct {
constexpr float missing = std::numeric_limits<float>::quiet_NaN();
constexpr size_t capacity = 70;
constexpr UINT sampleMessage = WM_APP + 1, trayMessage = WM_APP + 2;
std::wstring basePath();
std::wstring normalize(std::wstring value);
std::wstring number(double value, int digits = 1);
std::wstring memoryText(uint64_t bytes);
struct Config {
  bool dark = true, desktop = true, through = false;
  int x = 80, y = 80, width = 920, height = 760, columns = 4, seconds = 60;
  int cpuHeight = 80, memoryHeight = 80, rankingHeight = 240;
  int opacityPercent = 100, cardOpacityPercent = 100, fontSize = 14, fontChoice = 2;
  int cpuMetric = 0; // 0: Windows Utility, 1: logical-thread busy time.
  void load();
  void save() const;
};
struct Core {
  WORD group{};
  BYTE index{}, efficiency{};
  std::vector<int> threads;
  std::wstring label;
  wchar_t kind = L'C';
};
struct Reading {
  // load: maximum logical-thread occupancy within this physical core (0..100%).
  float load = missing, utility = missing, temp = missing, ghz = missing;
};
struct App {
  std::wstring name;
  uint64_t bytes = 0;
  float cpu = 0;
  int processes = 0;
};
struct Frame {
  uint64_t tick = 0, wall = 0, totalMemory = 0, usedMemory = 0;
  float cpu = missing, cpuUtility = missing, memory = missing;
  std::vector<Reading> cores;
  std::array<App, 5> topCpu;
};
struct State {
  std::mutex mutex;
  std::vector<Core> cores;
  std::vector<Frame> ring;
  size_t head = 0, count = 0;
  std::array<App, 5> topMemory;
  std::wstring status;
  uint64_t sequence = 0;
  double sampleMilliseconds = 0;
  const Frame &latest() const { return ring[(head + capacity - 1) % capacity]; }
  const Frame &at(size_t i) const { return ring[(head + capacity - count + i) % capacity]; }
  void init();
};
class Sampler {
  State &state;
  HANDLE stopEvent = nullptr;
  std::thread worker;
  void run();

public:
  explicit Sampler(State &s) : state(s) {};
  ~Sampler() { stop(); }
  void start(HWND hwnd);
  void stop();
  HWND target = nullptr;
};
void report(const State &state, const std::wstring &path, double cpu = 0, double working = 0,
            double commit = 0);
} // namespace ct
