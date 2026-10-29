// Documentation preview only: use the production renderer with synthetic data.
// The hidden window does not run the application message handler or sampler.
#define wWinMain CoreTraceOriginalEntry
#include "../native-src/ui.cpp"
#undef wWinMain

int main() {
  using namespace ct;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  UI ui;
  ui.verify = true;
  ui.config = Config{};
  ui.config.width = 920;
  ui.config.height = 1120;
  ui.config.cpuHeight = ui.config.memoryHeight = 110;
  ui.config.rankingHeight = 220;
  ui.config.fontSize = 18;
  ui.config.seconds = 60;
  ui.memorySpeed = L"5600 MT/s";
  ui.updateFonts();
  ui.state.cores.clear();
  for (int i = 0; i < 14; ++i) {
    Core core;
    core.kind = i < 6 ? L'P' : L'E';
    core.label = std::wstring(1, core.kind) + std::to_wstring(i < 6 ? i : i - 6);
    core.index = BYTE(i);
    ui.state.cores.push_back(core);
  }
  ui.state.count = 60;
  ui.state.head = 60;
  ui.state.status.clear();
  for (int i = 0; i < 60; ++i) {
    auto &f = ui.state.ring[i];
    f.tick = 100000 + i * 1000;
    f.totalMemory = 64ull << 30;
    f.memory = 31.2f + float(i) * 0.028f;
    f.usedMemory = uint64_t(f.totalMemory * f.memory / 100.0);
    f.cpuUtility = 46.f + 8.f * std::sin(i * .22f) + 3.f * std::sin(i * .91f);
    f.cpu = f.cpuUtility * .75f;
    f.cores.resize(14);
    for (int j = 0; j < 14; ++j) {
      auto &r = f.cores[j];
      float base = j < 3 ? 75.f : j < 6 ? 35.f : 22.f;
      r.utility = std::clamp(base + 15.f * std::sin(i * .31f + j) +
                                       8.f * std::sin(i * 1.73f + j * .7f), 2.f, 100.f);
      if (j == 0 && i >= 46)
        r.utility = 100.f;
      r.load = r.utility * .75f;
      r.temp = (j == 0 ? 77.f : j == 1 ? 67.f : j < 6 ? 52.f : 43.f) +
               6.f * std::sin(i * .14f + j * .4f);
      r.ghz = j < 6 ? 5.29f : 3.99f;
    }
  }
  const wchar_t *names[] = {L"Solver", L"Browser", L"Editor", L"Terminal", L"Viewer"};
  const uint64_t sizes[] = {6400, 2100, 980, 420, 160};
  for (int i = 0; i < 5; ++i) {
    ui.state.topMemory[i].name = names[i];
    ui.state.topMemory[i].bytes = sizes[i] << 20;
    ui.state.topMemory[i].processes = 1;
  }
  HWND window = CreateWindowExW(0, L"STATIC", L"CoreTrace documentation preview", WS_POPUP,
                                0, 0, ui.config.width, ui.config.height, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
  if (!window)
    return 1;
  ui.hwnd = window;
  ui.config.dark = true;
  ui.theme();
  ui.capture(basePath() + L"\\preview-dark.bmp");
  ui.config.dark = false;
  ui.theme();
  ui.capture(basePath() + L"\\preview-light.bmp");
  DestroyWindow(window);
  ui.hwnd = nullptr;
  return 0;
}
