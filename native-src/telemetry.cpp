#include "core.hpp"
namespace ct {
std::wstring basePath() {
  wchar_t p[32768];
  GetModuleFileNameW(nullptr, p, 32768);
  return std::filesystem::path(p).parent_path().wstring();
}
std::wstring normalize(std::wstring s) {
  auto a = s.find_first_not_of(L" \t\r\n");
  if (a == s.npos)
    return L"";
  s = s.substr(a, s.find_last_not_of(L" \t\r\n") - a + 1);
  s = std::filesystem::path(s).filename().wstring();
  if (s.size() > 4 && _wcsicmp(s.c_str() + s.size() - 4, L".exe") == 0)
    s.resize(s.size() - 4);
  return s;
}
static std::wstring lower(std::wstring s) {
  for (auto &c : s)
    c = towlower(c);
  return s;
}
std::wstring number(double v, int digits) {
  if (!std::isfinite(v))
    return L"—";
  std::wostringstream o;
  o << std::fixed << std::setprecision(digits) << v;
  return o.str();
}
std::wstring memoryText(uint64_t b) {
  return b >= 1073741824 ? number(b / 1073741824., 2) + L" GB" : number(b / 1048576., 0) + L" MB";
}
void Config::load() {
  auto p = basePath() + L"\\settings.ini";
  auto get = [&](const wchar_t *k, int d) {
    return (int)GetPrivateProfileIntW(L"CoreTrace", k, d, p.c_str());
  };
  cpuMetric = std::clamp(get(L"CpuMetric", 0), 0, 1);
  opacityPercent = std::clamp(get(L"OpacityPercent", 100), 20, 100);
  cardOpacityPercent = std::clamp(get(L"CardOpacityPercent", opacityPercent), 20, 100);
  fontChoice = std::clamp(get(L"FontChoice", 2), 0, 6);
  fontSize = std::clamp(get(L"FontSize", 14), 12, 22);
  dark = get(L"Dark", 1);
  desktop = get(L"Desktop", 1);
  through = get(L"ClickThrough", 0);
  x = get(L"X", 80);
  y = get(L"Y", 80);
  width = std::clamp(get(L"Width", 920), 380, 3840);
  height = std::clamp(get(L"Height", 760), 280, 2160);
  columns = std::clamp(get(L"Columns", 4), 1, 6);
  cpuHeight = std::clamp(get(L"CpuHeight", 80), 80, 400);
  memoryHeight = cpuHeight;
  rankingHeight = std::clamp(get(L"RankingHeight", 240), 200, 500);
  seconds = get(L"HistorySeconds", 60);
  if (seconds != 20 && seconds != 40 && seconds != 60)
    seconds = 60;
  desktop = true;
  through = false;
}
void Config::save() const {
  auto p = basePath() + L"\\settings.ini";
  if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::ofstream f(std::filesystem::path(p), std::ios::binary);
    f.put(char(0xff));
    f.put(char(0xfe));
  }
  auto put = [&](const wchar_t *k, int v) {
    WritePrivateProfileStringW(L"CoreTrace", k, std::to_wstring(v).c_str(), p.c_str());
  };
  put(L"CpuMetric", cpuMetric);
  put(L"OpacityPercent", opacityPercent);
  put(L"CardOpacityPercent", cardOpacityPercent);
  put(L"FontChoice", fontChoice);
  put(L"FontSize", fontSize);
  put(L"Dark", dark);
  put(L"Desktop", desktop);
  put(L"ClickThrough", through);
  put(L"X", x);
  put(L"Y", y);
  put(L"Width", width);
  put(L"Height", height);
  put(L"Columns", columns);
  put(L"HistorySeconds", seconds);
  put(L"CpuHeight", cpuHeight);
  put(L"MemoryHeight", cpuHeight);
  put(L"RankingHeight", rankingHeight);
  WritePrivateProfileStringW(L"CoreTrace", L"Watch", nullptr, p.c_str());
}
void State::init() {
  ULONG needed = 0;
  GetSystemCpuSetInformation(nullptr, 0, &needed, nullptr, 0);
  std::vector<BYTE> data(needed);
  if (!needed || !GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)data.data(), needed,
                                             &needed, nullptr, 0))
    throw std::runtime_error("CPU topology unavailable");
  std::map<std::pair<WORD, BYTE>, Core> grouped;
  std::vector<BYTE> classes;
  for (size_t i = 0; i + 20 <= data.size();) {
    auto p = (SYSTEM_CPU_SET_INFORMATION *)(data.data() + i);
    if (p->Size < 20 || i + p->Size > data.size())
      break;
    if (p->Type == CpuSetInformation) {
      auto &v = p->CpuSet;
      auto &c = grouped[{v.Group, v.CoreIndex}];
      c.group = v.Group;
      c.index = v.CoreIndex;
      c.efficiency = v.EfficiencyClass;
      c.threads.push_back(v.LogicalProcessorIndex);
      classes.push_back(v.EfficiencyClass);
    }
    i += p->Size;
  }
  std::sort(classes.begin(), classes.end());
  classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
  int pi = 0, ei = 0, ci = 0;
  for (auto &[_, c] : grouped) {
    c.kind = classes.size() == 2 ? (c.efficiency == classes.back() ? L'P' : L'E') : L'C';
    int i = c.kind == L'P' ? pi++ : c.kind == L'E' ? ei++ : ci++;
    c.label = std::wstring(1, c.kind) + std::to_wstring(i);
    cores.push_back(c);
  }
  if (cores.empty() || cores.size() > 256)
    throw std::runtime_error("Unsupported CPU count");
  ring.resize(capacity);
  for (auto &f : ring)
    f.cores.resize(cores.size());
}
class Sensors {
  HMODULE dll = nullptr;
  HANDLE handle = nullptr;
  using Open = HRESULT(WINAPI *)(HANDLE *);
  using Load = HRESULT(WINAPI *)(HANDLE, const UCHAR *, SIZE_T);
  using Execute = HRESULT(WINAPI *)(HANDLE, PCSTR, const ULONG64 *, SIZE_T, PULONG64, SIZE_T,
                                    PSIZE_T);
  using Close = HRESULT(WINAPI *)(HANDLE);
  Execute execute = nullptr;
  Close close = nullptr;
  std::vector<float> targets;
  double busMHz = 0;
  bool read(uint32_t reg, uint64_t &value) {
    if (!handle)
      return false;
    ULONG64 in = reg;
    SIZE_T count = 0;
    return SUCCEEDED(execute(handle, "ioctl_read_msr", &in, 1, &value, 1, &count)) && count == 1;
  }

public:
  explicit Sensors(const std::vector<Core> &cores) {
    unsigned a, b, c, d;
    __cpuid(0, a, b, c, d);
    char vendor[13]{};
    memcpy(vendor, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    if (strcmp(vendor, "GenuineIntel") != 0)
      return;
    __cpuid(1, a, b, c, d);
    unsigned family = (a >> 8) & 15, model = ((a >> 4) & 15) | ((a >> 12) & 240);
    if (family != 6 ||
        (model != 0x97 && model != 0x9a && model != 0xb7 && model != 0xba && model != 0xbf))
      return;
    wchar_t install[32768];
    DWORD bytes = sizeof(install);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO",
                     L"InstallLocation", RRF_RT_REG_SZ, nullptr, install, &bytes) != ERROR_SUCCESS)
      return;
    auto path = std::wstring(install) + L"\\PawnIOLib.dll";
    dll = LoadLibraryExW(path.c_str(), nullptr,
                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll)
      return;
    auto open = (Open)GetProcAddress(dll, "pawnio_open");
    auto load = (Load)GetProcAddress(dll, "pawnio_load");
    execute = (Execute)GetProcAddress(dll, "pawnio_execute");
    close = (Close)GetProcAddress(dll, "pawnio_close");
    if (!open || !load || !execute || !close)
      return;
    if (FAILED(open(&handle)))
      return;
    std::ifstream f(std::filesystem::path(basePath() + L"\\IntelMSR.bin"), std::ios::binary);
    std::vector<unsigned char> blob((std::istreambuf_iterator<char>(f)), {});
    if (blob.empty() || FAILED(load(handle, blob.data(), blob.size()))) {
      close(handle);
      handle = nullptr;
      return;
    }
    GROUP_AFFINITY old{}, set{};
    set.Group = cores[0].group;
    set.Mask = KAFFINITY(1) << cores[0].threads[0];
    if (!SetThreadGroupAffinity(GetCurrentThread(), &set, &old))
      return;
    uint64_t platform = 0;
    if (read(0xCE, platform)) {
      unsigned ratio = (platform >> 8) & 255;
      if (ratio) {
        LARGE_INTEGER q1, q2, freq;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&q1);
        auto t1 = __rdtsc();
        Sleep(100);
        auto t2 = __rdtsc();
        QueryPerformanceCounter(&q2);
        double tscMHz = (t2 - t1) / (double(q2.QuadPart - q1.QuadPart) / freq.QuadPart) / 1e6;
        busMHz = tscMHz / ratio;
        if (busMHz < 80 || busMHz > 150)
          busMHz = 0;
      }
    }
    SetThreadGroupAffinity(GetCurrentThread(), &old, nullptr);
    targets.assign(cores.size(), missing);
    for (size_t i = 0; i < cores.size(); i++) {
      set.Group = cores[i].group;
      set.Mask = KAFFINITY(1) << cores[i].threads[0];
      if (SetThreadGroupAffinity(GetCurrentThread(), &set, &old)) {
        uint64_t v = 0;
        if (read(0x1A2, v)) {
          auto t = (v >> 16) & 255;
          if (t >= 60 && t <= 125)
            targets[i] = float(t);
        }
        SetThreadGroupAffinity(GetCurrentThread(), &old, nullptr);
      }
    }
  }
  void sample(const std::vector<Core> &cores, std::vector<Reading> &values) {
    if (!handle || targets.size() != cores.size())
      return;
    for (size_t i = 0; i < cores.size(); i++) {
      GROUP_AFFINITY old{}, set{};
      set.Group = cores[i].group;
      set.Mask = KAFFINITY(1) << cores[i].threads[0];
      if (!SetThreadGroupAffinity(GetCurrentThread(), &set, &old))
        continue;
      uint64_t v = 0;
      if (read(0x19C, v) && (v & (1ull << 31)) && std::isfinite(targets[i]))
        values[i].temp = targets[i] - float((v >> 16) & 127);
      if (busMHz > 0 && read(0x198, v)) {
        unsigned ratio = (v >> 8) & 255;
        if (ratio > 0)
          values[i].ghz = float(ratio * busMHz / 1000.);
      }
      SetThreadGroupAffinity(GetCurrentThread(), &old, nullptr);
    }
  }
  ~Sensors() {
    if (handle && close)
      close(handle);
    if (dll)
      FreeLibrary(dll);
  }
};
// Windows x64 SYSTEM_PROCESS_INFORMATION leading fields. Validate buffer bounds before use.
struct Unicode {
  USHORT length, maxLength;
  PWSTR buffer;
};
struct ProcessInfo {
  ULONG next, threads;
  LONGLONG privateWs;
  ULONG faults, threadsPeak;
  ULONGLONG cycles;
  LARGE_INTEGER created, user, kernel;
  Unicode name;
  LONG priority;
  HANDLE pid, parent;
};
static_assert(offsetof(ProcessInfo, privateWs) == 8 && offsetof(ProcessInfo, name) == 56 &&
              offsetof(ProcessInfo, pid) == 80);
class Processes {
  using Query = LONG(NTAPI *)(ULONG, PVOID, ULONG, PULONG);
  Query query = (Query)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
  std::vector<BYTE> buffer = std::vector<BYTE>(1 << 20);
  struct Prev {
    uint64_t time, created;
  };
  std::unordered_map<uint64_t, Prev> prev;
  uint64_t prior = 0;
  int logical;

public:
  explicit Processes(int n) : logical(n) {}
  bool sample(std::vector<App> &apps) {
    ULONG length = 0;
    LONG result;
    while ((result = query(5, buffer.data(), (ULONG)buffer.size(), &length)) == LONG(0xC0000004)) {
      if (length > 64 * 1024 * 1024)
        return false;
      buffer.resize(std::max<size_t>(length + 65536, buffer.size() * 2));
    }
    if (result < 0)
      return false;
    uint64_t now = GetTickCount64();
    double elapsed = prior ? (now - prior) / 1000. : 0;
    prior = now;
    std::unordered_map<uint64_t, Prev> next;
    std::map<std::wstring, App> grouped;
    for (size_t offset = 0; offset + sizeof(ProcessInfo) <= buffer.size();) {
      auto p = (ProcessInfo *)(buffer.data() + offset);
      uint64_t pid = (ULONG_PTR)p->pid;
      auto addr = (uintptr_t)p->name.buffer;
      auto begin = (uintptr_t)buffer.data();
      if (pid && p->name.length % 2 == 0 && addr >= begin &&
          addr + p->name.length <= begin + buffer.size()) {
        std::wstring name = normalize(std::wstring(p->name.buffer, p->name.length / 2));
        uint64_t ticks = p->user.QuadPart + p->kernel.QuadPart;
        float cpu = 0;
        auto it = prev.find(pid);
        if (elapsed > 0 && it != prev.end() &&
            it->second.created == uint64_t(p->created.QuadPart) && ticks >= it->second.time)
          cpu = float((ticks - it->second.time) / 1e7 / elapsed / logical * 100.);
        next[pid] = {ticks, uint64_t(p->created.QuadPart)};
        auto &app = grouped[lower(name)];
        app.name = name;
        app.bytes += std::max<LONGLONG>(p->privateWs, 0);
        app.cpu += cpu;
        app.processes++;
      }
      if (!p->next)
        break;
      if (p->next < sizeof(ProcessInfo) || offset + p->next >= buffer.size())
        return false;
      offset += p->next;
    }
    prev.swap(next);
    apps.clear();
    apps.reserve(grouped.size());
    for (auto &[_, app] : grouped)
      apps.push_back(std::move(app));
    return true;
  }
};
void Sampler::start(HWND hwnd) {
  target = hwnd;
  stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  worker = std::thread([this] {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    try {
      run();
    } catch (const std::exception &) {
      std::lock_guard lock(state.mutex);
      state.status = L"采样异常，请重启程序";
      if (target)
        PostMessageW(target, sampleMessage, 0, 0);
    }
  });
}
void Sampler::stop() {
  if (stopEvent)
    SetEvent(stopEvent);
  if (worker.joinable())
    worker.join();
  if (stopEvent) {
    CloseHandle(stopEvent);
    stopEvent = nullptr;
  }
}
void Sampler::run() {
  PDH_HQUERY query = nullptr;
  if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
    return;
  std::vector<std::vector<PDH_HCOUNTER>> counters, utilityCounters;
  PDH_HCOUNTER totalUtility = nullptr;
  PdhAddEnglishCounterW(query, L"\\Processor Information(_Total)\\% Processor Utility", 0, &totalUtility);
  int logical = 0;
  for (auto &c : state.cores) {
    std::vector<PDH_HCOUNTER> cs, us;
    for (int t : c.threads) {
      PDH_HCOUNTER h = nullptr;
      auto path = L"\\Processor Information(" + std::to_wstring(c.group) + L"," +
                  std::to_wstring(t) + L")\\% Processor Time";
      PdhAddEnglishCounterW(query, path.c_str(), 0, &h);
      cs.push_back(h);
      PDH_HCOUNTER u = nullptr;
      auto utilityPath = L"\\Processor Information(" + std::to_wstring(c.group) + L"," +
                         std::to_wstring(t) + L")\\% Processor Utility";
      PdhAddEnglishCounterW(query, utilityPath.c_str(), 0, &u);
      us.push_back(u);
      logical++;
    }
    counters.push_back(cs);
    utilityCounters.push_back(us);
  }
  // Keep both metrics in each history frame so switching never mixes definitions.
  // Use Windows' _Total Utility directly; averaging capped per-thread Utility is incorrect.
  auto readPercent = [](PDH_HCOUNTER h) -> float {
    PDH_FMT_COUNTERVALUE v{};
    if (!h || PdhGetFormattedCounterValue(h, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) != ERROR_SUCCESS ||
        v.CStatus > 1 || !std::isfinite(v.doubleValue))
      return missing;
    return float(std::clamp(v.doubleValue, 0., 100.));
  };
  Sensors sensors(state.cores);
  Processes processes(logical);
  PdhCollectQueryData(query);
  Frame frame;
  frame.cores.resize(state.cores.size());
  std::vector<App> apps;
  processes.sample(apps);
  while (WaitForSingleObject(stopEvent, 1000) == WAIT_TIMEOUT) {
    LARGE_INTEGER started, ended, qpcFrequency;
    QueryPerformanceFrequency(&qpcFrequency);
    QueryPerformanceCounter(&started);
    bool cpuOk = PdhCollectQueryData(query) == ERROR_SUCCESS;
    float total = 0;
    int valid = 0;
    for (size_t i = 0; i < counters.size(); i++) {
      frame.cores[i] = {};
      double sum = 0, busiest = 0;
      bool ok = cpuOk;
      for (auto h : counters[i]) {
        PDH_FMT_COUNTERVALUE value{};
        if (!h ||
            PdhGetFormattedCounterValue(h, PDH_FMT_DOUBLE, nullptr, &value) != ERROR_SUCCESS ||
            value.CStatus > 1) {
          ok = false;
          break;
        }
        double load = std::clamp(value.doubleValue, 0., 100.);
        sum += load;
        busiest = std::max(busiest, load);
      }
      if (ok) {
        // Per-core charts show the busiest logical thread, not physical compute capacity.
        // Keep the package summary weighted by all logical processors below.
        frame.cores[i].load = float(busiest);
        total += float(sum);
        valid += (int)counters[i].size();
      }
    }
    frame.cpu = valid == logical ? total / logical : missing;
    frame.cpuUtility = cpuOk ? readPercent(totalUtility) : missing;
    for (size_t i = 0; i < utilityCounters.size(); i++) {
      float busiest = 0;
      bool ok = cpuOk;
      for (auto h : utilityCounters[i]) {
        float v = readPercent(h);
        if (!std::isfinite(v)) { ok = false; break; }
        busiest = std::max(busiest, v);
      }
      frame.cores[i].utility = ok ? busiest : missing;
    }
    sensors.sample(state.cores, frame.cores);
    MEMORYSTATUSEX m{sizeof(m)};
    if (GlobalMemoryStatusEx(&m)) {
      frame.totalMemory = m.ullTotalPhys;
      frame.usedMemory = m.ullTotalPhys - m.ullAvailPhys;
      frame.memory = float(100. * frame.usedMemory / frame.totalMemory);
    } else
      frame.memory = missing;
    bool procOk = processes.sample(apps);
    std::array<App, 5> topMem{};
    if (procOk) {
      std::partial_sort(apps.begin(), apps.begin() + std::min<size_t>(5, apps.size()), apps.end(),
                        [](auto &a, auto &b) { return a.bytes > b.bytes; });
      for (size_t i = 0; i < 5 && i < apps.size(); i++)
        topMem[i] = apps[i];
      std::partial_sort(apps.begin(), apps.begin() + std::min<size_t>(5, apps.size()), apps.end(),
                        [](auto &a, auto &b) { return a.cpu > b.cpu; });
      for (size_t i = 0; i < 5; i++)
        frame.topCpu[i] = i < apps.size() ? apps[i] : App{};
    }
    frame.tick = GetTickCount64();
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    frame.wall = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    {
      std::lock_guard lock(state.mutex);
      state.ring[state.head] = frame;
      state.head = (state.head + 1) % capacity;
      state.count = std::min(capacity, state.count + 1);
      state.topMemory = std::move(topMem);
      state.sequence++;
      QueryPerformanceCounter(&ended);
      state.sampleMilliseconds =
          1000. * (ended.QuadPart - started.QuadPart) / qpcFrequency.QuadPart;
      state.status = L"";
      if (std::any_of(frame.cores.begin(), frame.cores.end(),
                      [](auto &r) { return !std::isfinite(r.temp); }))
        state.status = L"部分温度不可用：请检查硬件支持 / PawnIO 权限";
      if (!procOk)
        state.status += L" 进程统计不可用";
      if (!std::isfinite(frame.cpu))
        state.status += L" CPU 忙碌时间计数器不可用";
      if (!std::isfinite(frame.cpuUtility))
        state.status += L" CPU Utility 计数器不可用";
    }
    if (target)
      PostMessageW(target, sampleMessage, 0, 0);
  }
  PdhCloseQuery(query);
}
void report(const State &s, const std::wstring &path, double cpu, double working, double commit) {
  std::ofstream f{std::filesystem::path(path)};
  f << "{\n  \"cpu_percent\": " << cpu << ",\n  \"working_set_mb\": " << working
    << ",\n  \"private_commit_mb\": " << commit << ",\n  \"sample_ms\": " << s.sampleMilliseconds
    << ",\n  \"samples\": " << s.count << ",\n  \"core_count\": " << s.cores.size()
    << ",\n  \"cores\": [";
  if (s.count) {
    auto &frame = s.latest();
    for (size_t i = 0; i < s.cores.size(); i++) {
      if (i)
        f << ',';
      auto r = frame.cores[i];
      f << "{\"load\":" << (std::isfinite(r.load) ? std::to_string(r.load) : "null")
        << ",\"temp\":" << (std::isfinite(r.temp) ? std::to_string(r.temp) : "null")
        << ",\"ghz\":" << (std::isfinite(r.ghz) ? std::to_string(r.ghz) : "null") << '}';
    }
    f << "],\n \"total_memory\": " << frame.totalMemory
      << ",\n \"used_memory\": " << frame.usedMemory << ",\n \"top_memory\": [";
    for (size_t i = 0; i < 5; i++) {
      if (i)
        f << ',';
      auto &a = s.topMemory[i];
      std::string name(a.name.begin(), a.name.end());
      for (char &c : name)
        if (c == '"' || c == '\\' || (unsigned char)c < 32)
          c = '_';
      f << "{\"name\":\"" << name << "\",\"bytes\":" << a.bytes << '}';
    }
    f << "],\n \"intervals_ms\": [";
    for (size_t i = 1; i < s.count; i++) {
      if (i > 1)
        f << ',';
      f << s.at(i).tick - s.at(i - 1).tick;
    }
    f << ']';
  } else
    f << ']';
  f << "\n}\n";
}
} // namespace ct
