#include "core.hpp"
#include "memory-speed.hpp"
#include <windowsx.h>
namespace ct {
static uint64_t cpuTime() {
  FILETIME c, e, k, u;
  GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
  return (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
         (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}
struct Hit {
  RECT rect;
  int kind;
  int index;
};
struct UI {
  std::wstring memorySpeed = readMemorySpeed();
  Config config;
  State state;
  Sampler sampler{state};
  HWND hwnd = nullptr, host = nullptr, tooltip = nullptr;
  HDC memoryDC = nullptr;
  HBITMAP bitmap = nullptr, oldBitmap = nullptr;
  int bitmapW = 0, bitmapH = 0, scroll = 0, contentHeight = 0;
  HFONT font = nullptr, small = nullptr, bold = nullptr;
  COLORREF bg, card, fg, muted, grid, area, blue = RGB(61, 145, 245);
  std::vector<Hit> hits;
  std::wstring tipText;
  std::vector<Frame> frozen;
  HFONT rankingFont = nullptr;
  int rankingFontPixels = 0;
  int draggingControl = 0;
  std::array<RECT, 3> controlTracks{};
  RECT fontControl{}, metricControl{};
  uint32_t *bitmapPixels = nullptr;
  HDC alphaDC = nullptr;
  HBITMAP alphaBitmap = nullptr, oldAlphaBitmap = nullptr;
  uint32_t *alphaPixels = nullptr;
  int alphaW = 0, alphaH = 0;
  bool layeredPresented = false;
  DWORD layeredError = 0;
  static constexpr const wchar_t *fontNames[] = {
      L"等线", L"等线 Light", L"Microsoft YaHei UI", L"Microsoft YaHei UI Light", L"微软雅黑",
      L"黑体", L"宋体"};
  static constexpr const wchar_t *fontLabels[] = {
      L"等线",     L"等线（细体）", L"微软雅黑 UI", L"微软雅黑 UI（细体）",
      L"微软雅黑", L"黑体",         L"宋体"};
  bool movingWindow = false;
  POINT moveStart{};
  RECT moveOriginal{};
  bool verify = false;
  int draggingSummary = -1, dragStartY = 0, dragStartHeight = 0;
  std::array<RECT, 3> summaryHandles{};
  bool adjusting = false;
  bool paused = false, benchmark = false, fullBenchmark = false, historySeeded = false;
  uint64_t startTick = 0, startCpu = 0;
  HMENU menu = nullptr, metricMenu = nullptr;
  NOTIFYICONDATAW tray{};
  ULONG_PTR savedStyle = 0;
  std::array<App, 5> frozenMemory;
  std::wstring uiStatus;
  std::array<HPEN, 3> tempPens{};
  HPEN gridPen = nullptr, bluePen = nullptr;
  HBRUSH bgBrush = nullptr, cardBrush = nullptr, areaBrush = nullptr;
  UI() {
    config.load();
    state.init();
    updateFonts();
    theme();
  }
  ~UI() {
    sampler.stop();
    releaseAlphaSurface();
    if (tray.hWnd)
      Shell_NotifyIconW(NIM_DELETE, &tray);
    if (bitmap) {
      SelectObject(memoryDC, oldBitmap);
      DeleteObject(bitmap);
    }
    if (memoryDC)
      DeleteDC(memoryDC);
    if (rankingFont)
      DeleteObject(rankingFont);
    for (auto p : tempPens)
      DeleteObject(p);
    for (auto p : {gridPen, bluePen})
      DeleteObject(p);
    for (auto b : {bgBrush, cardBrush, areaBrush})
      DeleteObject(b);
    for (auto f : {font, small, bold})
      DeleteObject(f);
    if (menu)
      DestroyMenu(menu);
  }
  void updateFonts() {
    rankingFontPixels = 0;
    if (alphaDC)
      SelectObject(alphaDC, GetStockObject(SYSTEM_FONT));
    if (memoryDC)
      SelectObject(memoryDC, GetStockObject(SYSTEM_FONT));
    for (auto f : {font, small, bold})
      if (f)
        DeleteObject(f);
    auto make = [&](int size, int weight) {
      return CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                         DEFAULT_PITCH, fontNames[config.fontChoice]);
    };
    font = make(config.fontSize, FW_NORMAL);
    small = make(std::max(10, config.fontSize * 11 / 14), FW_NORMAL);
    bold = make(config.fontSize + 1, FW_SEMIBOLD);
    if (tooltip)
      SendMessageW(tooltip, WM_SETFONT, (WPARAM)font, FALSE);
  }
  bool translucent() const {
    return config.opacityPercent < 100 || config.cardOpacityPercent < 100;
  }
  void releaseAlphaSurface() {
    if (alphaBitmap) {
      SelectObject(alphaDC, oldAlphaBitmap);
      DeleteObject(alphaBitmap);
    }
    if (alphaDC)
      DeleteDC(alphaDC);
    alphaDC = nullptr;
    alphaBitmap = nullptr;
    alphaPixels = nullptr;
    alphaW = alphaH = 0;
  }
  void applyTransparency() {
    if (!hwnd)
      return;
    auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    auto desired = translucent() ? ex | WS_EX_LAYERED : ex & ~WS_EX_LAYERED;
    if (ex != desired)
      SetWindowLongPtrW(hwnd, GWL_EXSTYLE, desired);
    if (!translucent())
      releaseAlphaSurface();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  static uint32_t pixelColor(COLORREF c) {
    return uint32_t(GetRValue(c)) << 16 | uint32_t(GetGValue(c)) << 8 | GetBValue(c);
  }
  COLORREF borderColor() const { return config.dark ? RGB(43, 51, 63) : RGB(226, 232, 240); }
  // Only backgrounds receive reduced alpha. Data, text, grid and controls stay readable.
  BYTE pixelAlpha(uint32_t rgb, int y) const {
    if (adjusting && y >= bitmapH - editorHeight)
      return 255;
    if (rgb == pixelColor(bg) || rgb == pixelColor(borderColor()))
      return BYTE((config.opacityPercent * 255 + 50) / 100);
    if (rgb == pixelColor(card))
      return BYTE((config.cardOpacityPercent * 255 + 50) / 100);
    return 255;
  }
  void presentAlpha(HDC destination) {
    RECT window;
    GetWindowRect(hwnd, &window);
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    int w = window.right - window.left, h = window.bottom - window.top;
    int ox = origin.x - window.left, oy = origin.y - window.top;
    if (!alphaDC || alphaW != w || alphaH != h) {
      releaseAlphaSurface();
      alphaDC = CreateCompatibleDC(destination);
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = w;
      info.bmiHeader.biHeight = -h;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      alphaBitmap =
          CreateDIBSection(destination, &info, DIB_RGB_COLORS, (void **)&alphaPixels, nullptr, 0);
      if (!alphaBitmap) {
        releaseAlphaSurface();
        layeredPresented = false;
        return;
      }
      oldAlphaBitmap = (HBITMAP)SelectObject(alphaDC, alphaBitmap);
      alphaW = w;
      alphaH = h;
    }
    // Retain native caption and resize hit testing while presenting the entire layered surface.
    RECT full{0, 0, w, h};
    FillRect(alphaDC, &full, bgBrush);
    SetBkMode(alphaDC, TRANSPARENT);
    if (adjusting && oy > 0)
      text(alphaDC, L"CoreTrace · 拖动标题或底部空白移动", ox + 8,
           std::max(1, (oy - config.fontSize) / 2), w - 32, muted);
    SelectObject(alphaDC, GetStockObject(SYSTEM_FONT));
    GdiFlush();
    for (size_t i = 0, n = size_t(w) * h; i < n; i++)
      alphaPixels[i] = (alphaPixels[i] & 0xffffff) | 0xff000000;
    for (int y = 0; y < bitmapH; y++) {
      if (y + oy < 0 || y + oy >= h)
        continue;
      auto src = bitmapPixels + size_t(y) * bitmapW;
      auto dst = alphaPixels + size_t(y + oy) * w;
      for (int x = 0; x < bitmapW; x++) {
        if (x + ox < 0 || x + ox >= w)
          continue;
        uint32_t rgb = src[x] & 0xffffff;
        unsigned a = pixelAlpha(rgb, y);
        unsigned b = ((rgb & 255) * a + 127) / 255, g = (((rgb >> 8) & 255) * a + 127) / 255,
                 r = (((rgb >> 16) & 255) * a + 127) / 255;
        dst[x + ox] = (a << 24) | (r << 16) | (g << 8) | b;
      }
    }
    POINT position{window.left, window.top}, source{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    layeredPresented = UpdateLayeredWindow(hwnd, nullptr, &position, &size, alphaDC, &source, 0,
                                           &blend, ULW_ALPHA) != FALSE;
    layeredError = layeredPresented ? 0 : GetLastError();
  }
  const wchar_t *metricLabel() const {
    return config.cpuMetric == 0 ? L"任务管理器" : L"忙碌时间";
  }
  void chooseMetric(int index) {
    config.cpuMetric = std::clamp(index, 0, 1);
    if (metricMenu)
      CheckMenuRadioItem(metricMenu, 20, 21, 20 + config.cpuMetric, MF_BYCOMMAND);
    tipText.clear();
    if (tooltip) SendMessageW(tooltip, TTM_POP, 0, 0);
    save();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void metricPopup() {
    POINT p{metricControl.left, metricControl.top};
    ClientToScreen(hwnd, &p);
    int id = TrackPopupMenu(metricMenu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                            p.x, p.y, 0, hwnd, nullptr);
    if (id == 20 || id == 21) chooseMetric(id - 20);
  }
  void chooseFont(int index) {
    config.fontChoice = std::clamp(index, 0, 6);
    updateFonts();
    save();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void fontPopup() {
    HMENU choices = CreatePopupMenu();
    for (int i = 0; i < 7; i++)
      AppendMenuW(choices, MF_STRING | (config.fontChoice == i ? MF_CHECKED : 0), 100 + i,
                  fontLabels[i]);
    POINT p{fontControl.left, fontControl.top};
    ClientToScreen(hwnd, &p);
    int id = TrackPopupMenu(choices, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                            p.x, p.y, 0, hwnd, nullptr);
    DestroyMenu(choices);
    if (id >= 100 && id < 107)
      chooseFont(id - 100);
  }
  void changeControl(int which, int x) {
    auto r = controlTracks[which - 1];
    double fraction = std::clamp(double(x - r.left) / std::max(1L, r.right - r.left), 0., 1.);
    if (which <= 2) {
      int &target = which == 1 ? config.opacityPercent : config.cardOpacityPercent;
      int opacity = 100 - int(std::lround(fraction * 80));
      if (opacity != target) {
        target = opacity;
        applyTransparency();
      }
    } else {
      int size = 12 + int(std::lround(fraction * 10));
      if (size != config.fontSize) {
        config.fontSize = size;
        updateFonts();
        RECT client;
        GetClientRect(hwnd, &client);
        int needed = minimumHeight(client.right) + (adjusting ? editorHeight : 0);
        if (client.bottom < needed) {
          POINT origin{0, 0};
          ClientToScreen(hwnd, &origin);
          placeClient(origin, client.right, needed);
        }
      }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void theme() {
    bg = config.dark ? RGB(22, 26, 33) : RGB(241, 244, 249);
    card = config.dark ? RGB(29, 34, 43) : RGB(255, 255, 255);
    fg = config.dark ? RGB(229, 235, 245) : RGB(29, 40, 58);
    muted = config.dark ? RGB(151, 165, 184) : RGB(99, 113, 133);
    grid = config.dark ? RGB(39, 47, 58) : RGB(236, 240, 245);
    area = config.dark ? RGB(28, 58, 92) : RGB(210, 229, 252);
    for (auto p : tempPens)
      if (p)
        DeleteObject(p);
    tempPens = {CreatePen(PS_SOLID, 1, RGB(51, 185, 98)), CreatePen(PS_SOLID, 1, RGB(242, 156, 47)),
                CreatePen(PS_SOLID, 1, RGB(242, 76, 85))};
    for (auto p : {gridPen, bluePen})
      if (p)
        DeleteObject(p);
    for (auto b : {bgBrush, cardBrush, areaBrush})
      if (b)
        DeleteObject(b);
    gridPen = CreatePen(PS_SOLID, 1, grid);
    bluePen = CreatePen(PS_SOLID, 1, blue);
    bgBrush = CreateSolidBrush(bg);
    cardBrush = CreateSolidBrush(card);
    areaBrush = CreateSolidBrush(area);
  }
  static constexpr int editorHeight = 212;
  int bodyHeight() const {
    RECT r;
    GetClientRect(hwnd, &r);
    return r.bottom - (adjusting ? editorHeight : 0);
  }
  void placeClient(POINT origin, int width, int height) {
    RECT outer{0, 0, width, height};
    AdjustWindowRectExForDpi(&outer, DWORD(GetWindowLongPtrW(hwnd, GWL_STYLE)), FALSE,
                             DWORD(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)), GetDpiForWindow(hwnd));
    SetWindowPos(hwnd, nullptr, origin.x + outer.left, origin.y + outer.top,
                 outer.right - outer.left, outer.bottom - outer.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  void save() {
    if (verify || benchmark)
      return;
    RECT r;
    GetClientRect(hwnd, &r);
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    config.x = origin.x;
    config.y = origin.y;
    config.width = r.right;
    config.height = r.bottom - (adjusting ? editorHeight : 0);
    config.save();
  }
  HWND lastForeground = nullptr;
  void lowerWindow() {
    if (adjusting)
      return;
    struct Search {
      HWND self, last;
    } search{hwnd, nullptr};
    EnumWindows(
        [](HWND w, LPARAM p) -> BOOL {
          auto &s = *(Search *)p;
          if (w == s.self || !IsWindowVisible(w) || IsIconic(w))
            return TRUE;
          wchar_t cls[128];
          GetClassNameW(w, cls, 128);
          if (wcscmp(cls, L"WorkerW") == 0 || wcscmp(cls, L"Progman") == 0 ||
              wcsstr(cls, L"TrayWnd") || FindWindowExW(w, nullptr, L"SHELLDLL_DefView", nullptr))
            return TRUE;
          if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST)
            return TRUE;
          s.last = w;
          return TRUE;
        },
        (LPARAM)&search);
    SetWindowPos(hwnd, search.last ? search.last : HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  void attach() { lowerWindow(); }
  void detach() {}
  void hideWindow() { ShowWindow(hwnd, SW_HIDE); }
  void through() {
    auto ex =
        GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & ~(WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOPMOST);
    if (adjusting)
      ex = (ex | WS_EX_APPWINDOW) & ~(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    else
      ex = (ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE) & ~WS_EX_APPWINDOW;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    applyTransparency();
  }
  int columnsFor(int width) const {
    int limit = std::clamp((width - 24) / 170, 1, 8), best = 1, bestRows = INT_MAX,
        bestSlots = INT_MAX;
    for (int cols = 1; cols <= limit; cols++) {
      int rows = 0;
      for (wchar_t k : {L'P', L'E', L'C'}) {
        int n = 0;
        for (auto &c : state.cores)
          if (c.kind == k)
            n++;
        rows += (n + cols - 1) / cols;
      }
      if (rows < bestRows || (rows == bestRows && rows * cols < bestSlots)) {
        best = cols;
        bestRows = rows;
        bestSlots = rows * cols;
      }
    }
    return best;
  }
  int rowsFor(int width) const {
    int rows = 0, cols = columnsFor(width);
    for (wchar_t k : {L'P', L'E', L'C'}) {
      int n = 0;
      for (auto &c : state.cores)
        if (c.kind == k)
          n++;
      rows += (n + cols - 1) / cols;
    }
    return rows;
  }
  int groupSpacing() const {
    int groups = 0;
    for (wchar_t k : {L'P', L'E', L'C'})
      if (std::any_of(state.cores.begin(), state.cores.end(), [&](auto &c) { return c.kind == k; }))
        groups++;
    return std::max(0, groups - 1) * 18;
  }
  bool menuDragAt(POINT p) const {
    if (!adjusting)
      return false;
    RECT c;
    GetClientRect(hwnd, &c);
    int menuY = c.bottom - editorHeight;
    if (p.y < menuY || p.y >= c.bottom || p.x < 0 || p.x >= c.right)
      return false;
    if (PtInRect(&fontControl, p) || PtInRect(&metricControl, p))
      return false;
    for (auto r : controlTracks) {
      InflateRect(&r, 6, 5);
      if (PtInRect(&r, p))
        return false;
    }
    if (p.y < menuY + 40) {
      int step = std::min(76, int(c.right - 24) / 6);
      if (p.x >= 12 + step && p.x < 12 + 6 * step)
        return false;
    }
    return true;
  }
  void moveFromMenu(POINT current) {
    int x = moveOriginal.left + current.x - moveStart.x,
        y = moveOriginal.top + current.y - moveStart.y;
    RECT outer;
    GetWindowRect(hwnd, &outer);
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    int insetX = origin.x - outer.left, insetY = origin.y - outer.top;
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromPoint(current, MONITOR_DEFAULTTONEAREST), &monitor)) {
      if (abs(y + insetY - monitor.rcWork.top) <= 10)
        y = monitor.rcWork.top - insetY;
      if (abs(x + insetX - monitor.rcWork.left) <= 10)
        x = monitor.rcWork.left - insetX;
    }
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  int minimumHeight(int width) const {
    return 57 + groupSpacing() + 2 * config.cpuHeight + config.rankingHeight +
           rowsFor(width) * (config.fontSize + 52) + (width < 650 ? 36 : 0);
  }
  void beginAdjust() {
    if (!adjusting) {
      RECT r;
      GetClientRect(hwnd, &r);
      POINT origin{0, 0};
      ClientToScreen(hwnd, &origin);
      adjusting = true;
      through();
      SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_THICKFRAME);
      placeClient(origin, r.right, r.bottom + editorHeight);
    }
    SetWindowTextW(hwnd, L"CoreTrace");
    ShowWindow(hwnd, SW_RESTORE);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void confirmAdjust() {
    if (!adjusting)
      return;
    RECT r;
    GetClientRect(hwnd, &r);
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    adjusting = false;
    through();
    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    placeClient(origin, r.right, r.bottom - editorHeight);
    SetWindowTextW(hwnd, L"CoreTrace");
    save();
    lowerWindow();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void text(HDC dc, std::wstring s, int x, int y, int width, COLORREF color, HFONT f = nullptr) {
    SetTextColor(dc, color);
    SelectObject(dc, f ? f : font);
    RECT r{x, y, x + width, y + 48};
    DrawTextW(dc, s.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  void box(HDC dc, RECT r) {
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, borderColor());
    SelectObject(dc, cardBrush);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 10, 10);
  }
  const Frame &frame(size_t i) const { return paused ? frozen[i] : state.at(i); }
  size_t frames() const { return paused ? frozen.size() : state.count; }
  float value(const Frame &f, int kind, int index, bool temp = false) {
    if (kind == 0)
      return config.cpuMetric == 0 ? f.cpuUtility : f.cpu;
    if (kind == 1)
      return f.memory;
    if (kind == 2)
      return temp ? f.cores[index].temp
                  : config.cpuMetric == 0 ? f.cores[index].utility : f.cores[index].load;
    return missing;
  }
  int level(float t) { return t < 65 ? 0 : t < 80 ? 1 : 2; }
  void plot(HDC dc, RECT r, int kind, int index, bool axes = true) {
    RECT viewport;
    GetClientRect(hwnd, &viewport);
    if (r.bottom < 8 || r.top > viewport.bottom || r.right - r.left < 20)
      return;
    hits.push_back({r, kind, index});
    // Mark the full-scale boundary; a 100% trace is drawn over it below.
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, config.dark ? RGB(65, 78, 94) : RGB(180, 190, 203));
    MoveToEx(dc, r.left, r.top, nullptr);
    LineTo(dc, r.right, r.top);
    SelectObject(dc, gridPen);
    for (int i = 1; i <= 3; i++) {
      int y = r.top + (r.bottom - r.top) * i / 4;
      MoveToEx(dc, r.left, y, nullptr);
      LineTo(dc, r.right, y);
    }
    if (kind < 2)
      for (int i = 1; i <= 3; i++) {
        int x = r.left + (r.right - r.left) * i / 4;
        MoveToEx(dc, x, r.top, nullptr);
        LineTo(dc, x, r.bottom);
      }
    auto count = frames();
    if (!count)
      return;
    auto end = frame(count - 1).tick;
    auto begin = end - std::min<uint64_t>(end, uint64_t(config.seconds) * 1000);
    auto x = [&](uint64_t tick) {
      return r.right - int((end - tick) * double(r.right - r.left) / (config.seconds * 1000.));
    };
    auto y = [&](float v) {
      return r.bottom - int(std::clamp(v, 0.f, 100.f) * (r.bottom - r.top) / 100.f);
    };
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right + 1, r.bottom + 1);
    // One polygon per continuous run; no per-frame animation and no off-screen graph rendering.
    std::vector<POINT> points;
    points.reserve(std::min<size_t>(count + 2, capacity + 2));
    auto flush = [&] {
      if (points.size() > 1) {
        std::vector<POINT> poly;
        poly.reserve(points.size() + 2);
        poly.push_back({points.front().x, r.bottom});
        poly.insert(poly.end(), points.begin(), points.end());
        poly.push_back({points.back().x, r.bottom});
        SelectObject(dc, GetStockObject(NULL_PEN));
        SelectObject(dc, areaBrush);
        Polygon(dc, poly.data(), (int)poly.size());
        SelectObject(dc, bluePen);
        Polyline(dc, points.data(), (int)points.size());
      }
      points.clear();
    };
    uint64_t prior = 0;
    for (size_t i = 0; i < count; i++) {
      auto &f = frame(i);
      if (f.tick < begin)
        continue;
      float v = value(f, kind, index);
      if (!std::isfinite(v) || (prior && f.tick - prior > 6500)) {
        flush();
        prior = 0;
        if (!std::isfinite(v))
          continue;
      }
      points.push_back({x(f.tick), y(v)});
      prior = f.tick;
    }
    flush();
    if (kind == 2) {
      // Reduce samples sharing a screen pixel, preserving both extrema and endpoints.
      struct TPoint {
        int x;
        float t;
      };
      std::vector<TPoint> run;
      run.reserve(r.right - r.left + 4);
      auto drawTemperature = [&] {
        for (size_t i = 1; i < run.size(); i++) {
          auto a = run[i - 1], b = run[i];
          float av = a.t, bv = b.t;
          std::array<float, 4> cuts{0, 1, 1, 1};
          int n = 2;
          if (av != bv)
            for (float t : {65.f, 80.f}) {
              float u = (t - av) / (bv - av);
              if (u > 0 && u < 1)
                cuts[n++] = u;
            }
          std::sort(cuts.begin(), cuts.begin() + n);
          for (int k = 1; k < n; k++) {
            auto u = cuts[k - 1], v = cuts[k];
            SelectObject(dc, tempPens[level(av + (bv - av) * (u + v) / 2)]);
            MoveToEx(dc, a.x + int((b.x - a.x) * u), y(av + (bv - av) * u), nullptr);
            LineTo(dc, a.x + int((b.x - a.x) * v), y(av + (bv - av) * v));
          }
        }
        run.clear();
      };
      std::array<TPoint, 4> bucket{};
      int bucketX = INT_MIN;
      size_t ordinal = 0, minOrder = 0, maxOrder = 0;
      uint64_t lastTick = 0;
      auto emit = [&](TPoint p) {
        if (run.empty() || run.back().x != p.x || run.back().t != p.t)
          run.push_back(p);
      };
      auto flushBucket = [&] {
        if (bucketX == INT_MIN)
          return;
        emit(bucket[0]);
        if (minOrder < maxOrder) {
          emit(bucket[1]);
          emit(bucket[2]);
        } else {
          emit(bucket[2]);
          emit(bucket[1]);
        }
        emit(bucket[3]);
        bucketX = INT_MIN;
      };
      for (size_t i = 0; i < count; i++) {
        auto &f = frame(i);
        if (f.tick < begin)
          continue;
        float t = f.cores[index].temp;
        if (!std::isfinite(t) || (lastTick && f.tick - lastTick > 6500)) {
          flushBucket();
          drawTemperature();
          lastTick = 0;
          if (!std::isfinite(t))
            continue;
        }
        TPoint p{x(f.tick), t};
        if (p.x != bucketX) {
          flushBucket();
          bucketX = p.x;
          bucket.fill(p);
          ordinal = minOrder = maxOrder = 0;
        } else {
          ordinal++;
          if (t < bucket[1].t) {
            bucket[1] = p;
            minOrder = ordinal;
          }
          if (t > bucket[2].t) {
            bucket[2] = p;
            maxOrder = ordinal;
          }
          bucket[3] = p;
        }
        lastTick = f.tick;
      }
      flushBucket();
      drawTemperature();
    }
    RestoreDC(dc, saved);
  }

  void paint(HDC destination) {
    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right, h = client.bottom;
    if (w <= 0 || h <= 0)
      return;
    if (!memoryDC)
      memoryDC = CreateCompatibleDC(destination);
    if (!bitmap || bitmapW != w || bitmapH != h) {
      if (bitmap) {
        SelectObject(memoryDC, oldBitmap);
        DeleteObject(bitmap);
      }
      BITMAPINFO info{};
      info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth = w;
      info.bmiHeader.biHeight = -h;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      bitmap =
          CreateDIBSection(destination, &info, DIB_RGB_COLORS, (void **)&bitmapPixels, nullptr, 0);
      if (!bitmap)
        return;
      oldBitmap = (HBITMAP)SelectObject(memoryDC, bitmap);
      bitmapW = w;
      bitmapH = h;
    }
    HDC dc = memoryDC;
    FillRect(dc, &client, bgBrush);
    SetBkMode(dc, TRANSPARENT);
    hits.clear();
    int px = std::clamp((config.rankingHeight - 42) / 10 * config.fontSize / 14, 14,
                        std::min(40, ((config.rankingHeight - 42) / 5 - 14) * 4 / 5));
    if (px != rankingFontPixels) {
      SelectObject(dc, font);
      if (rankingFont)
        DeleteObject(rankingFont);
      rankingFont = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                DEFAULT_PITCH, fontNames[config.fontChoice]);
      rankingFontPixels = px;
    }
    int saved = SaveDC(dc);
    int menuY = h - (adjusting ? editorHeight : 0);
    int contentH = menuY;
    int top = 8;
    if (adjusting) {
      int step = std::min(76, (w - 24) / 6), x = 12;
      for (auto &label : std::array<std::wstring, 6>{
               L"CoreTrace", L"主题", paused ? L"继续" : L"暂停",
               config.seconds == 60 ? (step < 70 && config.fontSize > 18 ? L"1分" : L"1分钟")
                                    : std::to_wstring(config.seconds) + L"秒",
               config.fontSize > 18 || step < 70 ? L"固定" : L"确认固定", L"退出"}) {
        text(dc, label, x, menuY + 10, step - 3, label == L"退出" ? RGB(224, 92, 100) : fg,
             label == L"CoreTrace" ? small : font);
        x += step;
      }
      for (int i = 0; i < 3; i++) {
        int y = menuY + 56 + i * 34;
        controlTracks[i] = {190, y - 7, w - 24, y + 7};
        auto r = controlTracks[i];
        text(dc,
             i == 0   ? L"外框透明 " + std::to_wstring(100 - config.opacityPercent) + L"%"
             : i == 1 ? L"卡片透明 " + std::to_wstring(100 - config.cardOpacityPercent) + L"%"
                      : L"字号 " + std::to_wstring(config.fontSize),
             12, y - 14, 174, fg);
        SelectObject(dc, gridPen);
        MoveToEx(dc, r.left, y, nullptr);
        LineTo(dc, r.right, y);
        double f = i == 0   ? (100 - config.opacityPercent) / 80.
                   : i == 1 ? (100 - config.cardOpacityPercent) / 80.
                            : (config.fontSize - 12) / 10.;
        int thumb = r.left + int((r.right - r.left) * f);
        SelectObject(dc, bluePen);
        MoveToEx(dc, r.left, y, nullptr);
        LineTo(dc, thumb, y);
        RECT knob{thumb - 4, y - 6, thumb + 5, y + 7};
        SetDCBrushColor(dc, blue);
        FillRect(dc, &knob, (HBRUSH)GetStockObject(DC_BRUSH));
      }
      fontControl = {12, menuY + 144, w - 24, menuY + 176};
      text(dc, L"字体  " + std::wstring(fontLabels[config.fontChoice]) + L"  ▾", 12, menuY + 146,
           w - 36, fg);
      metricControl = {12, menuY + 178, w - 24, menuY + 210};
      text(dc, L"CPU 统计：" + std::wstring(metricLabel()) + L"  ▾", 12, menuY + 180, w - 36, fg);
    }
    {
      std::lock_guard lock(state.mutex);
      if (!frames())
        text(dc, L"正在采集真实数据…", 16, top + 12, w - 32, fg);
      else {
        auto &latest = frame(frames() - 1);
        int y = top;
        bool narrow = w < 650;
        auto summary = [&](int kind, std::wstring title) {
          int sh = config.cpuHeight + (narrow ? 18 : 0);
          box(dc, {12, y, w - 12, y + sh});
          if (kind == 0 && !adjusting) {
            RECT edit{20, y + 8, 40, y + 28};
            SelectObject(dc, gridPen);
            SelectObject(dc, cardBrush);
            RoundRect(dc, edit.left, edit.top, edit.right, edit.bottom, 5, 5);
            SelectObject(dc, bluePen);
            MoveToEx(dc, 24, y + 23, nullptr);
            LineTo(dc, 33, y + 14);
            MoveToEx(dc, 26, y + 24, nullptr);
            LineTo(dc, 35, y + 15);
          }
          text(dc, title, kind == 0 ? 52 : 24, y + 7,
               kind == 1 ? w - 225
               : narrow  ? w - 76
                         : w - 300,
               fg, bold);
          if (kind == 0) {
            float highest = missing;
            for (auto &reading : latest.cores)
              if (std::isfinite(reading.ghz) && (!std::isfinite(highest) || reading.ghz > highest))
                highest = reading.ghz;
            int frequencyY = y + (narrow ? config.fontSize + 15 : 7);
            RECT r{w - 270, frequencyY, w - 25, frequencyY + 32};
            auto label = L"最高主频 " + number(highest, 2) + L" GHz";
            SetTextColor(dc, muted);
            SelectObject(dc, narrow ? small : font);
            DrawTextW(dc, label.c_str(), -1, &r, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);
          }
          if (kind == 1) {
            RECT r{w - 188, y + 7, w - 25, y + 31};
            SetTextColor(dc, muted);
            SelectObject(dc, font);
            DrawTextW(dc, memorySpeed.c_str(), -1, &r, DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS);
            if (narrow)
              text(dc, memoryText(latest.usedMemory) + L" / " + memoryText(latest.totalMemory), 24,
                   y + config.fontSize + 15, w - 48, muted, small);
          }
          plot(dc,
               {24,
                y + (narrow ? config.fontSize + std::max(10, config.fontSize * 11 / 14) + 25
                            : config.fontSize + 20),
                w - 25, y + sh - 12},
               kind, 0);
          summaryHandles[kind] = kind == 0 ? RECT{12, y + sh - 4, w - 12, y + sh + 5} : RECT{};
          if (adjusting && kind == 0) {
            SelectObject(dc, GetStockObject(DC_PEN));
            SetDCPenColor(dc, muted);
            MoveToEx(dc, w / 2 - 18, y + sh - 2, nullptr);
            LineTo(dc, w / 2 + 18, y + sh - 2);
          }
          y += sh + 8;
        };
        summary(0, std::wstring(config.cpuMetric == 0 ? L"CPU 总体  " : L"CPU 忙碌  ") + number(value(latest, 0, 0), 0) + L"%");
        summary(1, L"内存  " + number(latest.memory) + L"%" +
                       (narrow ? L""
                               : L" · " + memoryText(latest.usedMemory) + L" / " +
                                     memoryText(latest.totalMemory)));
        text(dc, L"温度", 16, y, w - 32, muted, small);
        text(dc, L"<65°C", 70, y, 76, RGB(51, 185, 98), small);
        text(dc, L"65–<80°C", 150, y, 90, RGB(242, 156, 47), small);
        text(dc, L"≥80°C", 250, y, 80, RGB(242, 76, 85), small);
        y += 23;
        int cols = columnsFor(w), rows = rowsFor(w), cw = (w - 24 - (cols - 1) * 8) / cols;
        int rowHeight = std::max(56, (contentH - y - config.rankingHeight - 10 - groupSpacing()) /
                                         std::max(1, rows));
        bool firstGroup = true;
        for (wchar_t kind : {L'P', L'E', L'C'}) {
          if (std::none_of(state.cores.begin(), state.cores.end(),
                           [&](auto &c) { return c.kind == kind; }))
            continue;
          if (!firstGroup)
            y += 18;
          firstGroup = false;
          int n = 0;
          for (size_t i = 0; i < state.cores.size(); i++) {
            auto &c = state.cores[i];
            if (c.kind != kind)
              continue;
            int cx = 12 + n % cols * (cw + 8), cy = y + n / cols * rowHeight;
            box(dc, {cx, cy, cx + cw, cy + rowHeight - 6});
            text(dc, c.label, cx + 10, cy + 6, config.fontSize * 3, fg);
            int frequencyX = cx + 10 + config.fontSize * (c.label.size() > 2 ? 3 : 2) + 6;
            text(dc, number(latest.cores[i].ghz, 2) + L" GHz", frequencyX, cy + 6,
                 cx + cw - 10 - frequencyX, muted);
            plot(dc, {cx + 8, cy + config.fontSize + 15, cx + cw - 8, cy + rowHeight - 13}, 2,
                 (int)i, false);
            n++;
          }
          y += (n + cols - 1) / cols * rowHeight;
        }
        int rankingTop = y;
        box(dc, {12, y, w - 12, y + config.rankingHeight - 4});
        summaryHandles[2] = {12, y - 4, w - 12, y + 5};
        if (adjusting) {
          SelectObject(dc, GetStockObject(DC_PEN));
          SetDCPenColor(dc, muted);
          MoveToEx(dc, w / 2 - 18, y + 2, nullptr);
          LineTo(dc, w / 2 + 18, y + 2);
        }
        text(dc, L"内存占用前五", 24, y + 8, w - 48, fg, bold);
        y += 34;
        int rankingRow = (config.rankingHeight - 42) / 5;
        const auto &ranking = paused ? frozenMemory : state.topMemory;
        uint64_t maximum = 0;
        for (auto &app : ranking)
          maximum = std::max(maximum, app.bytes);
        int valuesWidth = std::min((w - 48) * 3 / 5, rankingFontPixels * 13);
        for (auto &app : ranking) {
          text(dc, app.name, 24, y, w - 60 - valuesWidth, fg, rankingFont);
          RECT valueRect{w - 24 - valuesWidth, y, w - 24, y + rankingRow - 9};
          SetTextColor(dc, muted);
          SelectObject(dc, rankingFont);
          auto valueText =
              memoryText(app.bytes) + L" · " + number(100. * app.bytes / latest.totalMemory) + L"%";
          DrawTextW(dc, valueText.c_str(), -1, &valueRect,
                    DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
          RECT track{24, y + rankingRow - 10, w - 24, y + rankingRow - 4};
          SelectObject(dc, GetStockObject(NULL_PEN));
          SelectObject(dc, areaBrush);
          RoundRect(dc, track.left, track.top, track.right, track.bottom, 6, 6);
          if (maximum && app.bytes) {
            RECT bar = track;
            bar.right = bar.left + int(double(track.right - track.left) * app.bytes / maximum);
            SetDCBrushColor(dc, blue);
            SelectObject(dc, GetStockObject(DC_BRUSH));
            RoundRect(dc, bar.left, bar.top, bar.right, bar.bottom, 6, 6);
          }
          y += rankingRow;
        }
        y = rankingTop + config.rankingHeight;
        if (!state.status.empty())
          text(dc, state.status, 16, y, w - 32, muted, small);
      }
    }
    RestoreDC(dc, saved);
    if (translucent())
      presentAlpha(destination);
    else
      BitBlt(destination, 0, 0, w, h, dc, 0, 0, SRCCOPY);
  }
  void hover(int x, int y) {
    std::lock_guard lock(state.mutex);
    for (auto &hit : hits) {
      POINT p{x, y};
      if (!PtInRect(&hit.rect, p) || !frames())
        continue;
      auto end = frame(frames() - 1).tick;
      auto target = end - uint64_t((hit.rect.right - x) * double(config.seconds * 1000) /
                                   (hit.rect.right - hit.rect.left));
      size_t best = 0;
      uint64_t distance = UINT64_MAX;
      for (size_t i = 0; i < frames(); i++) {
        auto tick = frame(i).tick;
        auto diff = tick > target ? tick - target : target - tick;
        if (diff < distance) {
          distance = diff;
          best = i;
        }
      }
      if (distance > 3500)
        return;
      auto &f = frame(best);
      FILETIME utc{DWORD(f.wall), DWORD(f.wall >> 32)}, local;
      FileTimeToLocalFileTime(&utc, &local);
      SYSTEMTIME st;
      FileTimeToSystemTime(&local, &st);
      wchar_t time[32];
      swprintf(time, 32, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
      std::wstring value = time;
      if (hit.kind == 2) {
        auto r = f.cores[hit.index];
        value += L"  " + state.cores[hit.index].label + (config.cpuMetric == 0 ? L"  最忙线程 Utility " : L"  最忙线程忙碌时间 ") +
                 number(this->value(f, 2, hit.index)) + L"%  " +
                 number(r.temp) + L"°C  " + number(r.ghz, 2) + L" GHz";
      } else
        value += L"  " + std::wstring(hit.kind == 0 ? metricLabel() : L"内存") + L" " +
                 number(this->value(f, hit.kind, 0)) + L"%";
      value += L"\n当时CPU前五（忙碌时间）：";
      for (auto &a : f.topCpu)
        if (!a.name.empty())
          value += a.name + L" " + number(a.cpu) + L"%  ";
      if (tipText == value)
        return;
      tipText = value;
      TOOLINFOW ti{sizeof(ti)};
      ti.hwnd = hwnd;
      ti.uId = 1;
      ti.lpszText = tipText.data();
      SendMessageW(tooltip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
      POINT pt{x + 12, y + 18};
      ClientToScreen(hwnd, &pt);
      SendMessageW(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
      SendMessageW(tooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
      return;
    }
    TOOLINFOW ti{sizeof(ti)};
    ti.hwnd = hwnd;
    ti.uId = 1;
    SendMessageW(tooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
    tipText.clear();
  }
  void pause() {
    std::lock_guard lock(state.mutex);
    paused = !paused;
    frozen.clear();
    if (paused) {
      frozen.reserve(state.count);
      for (size_t i = 0; i < state.count; i++)
        frozen.push_back(state.at(i));
      frozenMemory = state.topMemory;
    } else
      std::vector<Frame>().swap(frozen);
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void popup() {
    POINT p;
    GetCursorPos(&p);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
  }
  void command(int id) {
    switch (id) {
    case 1:
      beginAdjust();
      break;
    case 2:
      config.dark = !config.dark;
      theme();
      break;
    case 3:
      pause();
      break;
    case 4:
      config.seconds = config.seconds == 20 ? 40 : config.seconds == 40 ? 60 : 20;
      break;
    case 6:
    case 10:
      confirmAdjust();
      break;
    case 20:
    case 21:
      chooseMetric(id - 20);
      return;
    case 12:
      config.opacityPercent = config.cardOpacityPercent = 100;
      applyTransparency();
      break;
    case 11:
      hideWindow();
      break;
    case 9:
      DestroyWindow(hwnd);
      return;
    }
    save();
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  void capture(const std::wstring &file, bool previewAlpha = false) {
    HDC dc = GetDC(hwnd);
    paint(dc);
    ReleaseDC(hwnd, dc);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bitmapW;
    info.bmiHeader.biHeight = -bitmapH;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<BYTE> pixels(size_t(bitmapW) * bitmapH * 4);
    SelectObject(memoryDC, oldBitmap);
    GetDIBits(memoryDC, bitmap, 0, bitmapH, pixels.data(), &info, DIB_RGB_COLORS);
    SelectObject(memoryDC, bitmap);
    if (previewAlpha) {
      // Diagnostic checkerboard uses the live alpha rules; no screenshot processing is needed.
      for (int y = 0; y < bitmapH; y++)
        for (int x = 0; x < bitmapW; x++) {
          auto p = pixels.data() + (size_t(y) * bitmapW + x) * 4;
          unsigned a = pixelAlpha(uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0], y);
          unsigned backdrop = ((x / 24 + y / 24) % 2) ? 125 : 165;
          for (int k = 0; k < 3; k++)
            p[k] = BYTE((p[k] * a + backdrop * (255 - a) + 127) / 255);
          p[3] = 255;
        }
    }
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + (DWORD)pixels.size();
    std::ofstream out(std::filesystem::path(file), std::ios::binary);
    out.write((char *)&header, sizeof(header));
    out.write((char *)&info.bmiHeader, sizeof(info.bmiHeader));
    out.write((char *)pixels.data(), pixels.size());
  }
  static LRESULT CALLBACK procedure(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    auto self = (UI *)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      self = (UI *)((CREATESTRUCTW *)lp)->lpCreateParams;
      self->hwnd = w;
      SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (!self)
      return DefWindowProcW(w, msg, wp, lp);
    return self->message(msg, wp, lp);
  }
  LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
      menu = CreatePopupMenu();
      for (auto [id, label] :
           std::array<std::pair<int, const wchar_t *>, 6>{{{1, L"移动 / 调整窗口"},
                                                           {10, L"确认固定"},
                                                           {2, L"切换主题"},
                                                           {12, L"恢复不透明"},
                                                           {11, L"隐藏"},
                                                           {9, L"退出"}}})
        AppendMenuW(menu, MF_STRING, id, label);
      metricMenu = CreatePopupMenu();
      AppendMenuW(metricMenu, MF_STRING, 20, L"任务管理器（Utility）");
      AppendMenuW(metricMenu, MF_STRING, 21, L"忙碌时间（Time）");
      AppendMenuW(menu, MF_POPUP, (UINT_PTR)metricMenu, L"CPU 统计方式");
      CheckMenuRadioItem(metricMenu, 20, 21, 20 + config.cpuMetric, MF_BYCOMMAND);
      tray.cbSize = sizeof(tray);
      tray.hWnd = hwnd;
      tray.uID = 1;
      tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
      tray.uCallbackMessage = trayMessage;
      tray.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101), IMAGE_ICON,
                                     GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                     LR_SHARED);
      SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)tray.hIcon);
      SendMessageW(hwnd, WM_SETICON, ICON_BIG,
                   (LPARAM)LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101)));
      wcscpy_s(tray.szTip, L"CoreTrace");
      Shell_NotifyIconW(NIM_ADD, &tray);
      tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
      TOOLINFOW ti{sizeof(ti)};
      ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
      ti.hwnd = hwnd;
      ti.uId = 1;
      ti.lpszText = (LPWSTR)L"";
      SendMessageW(tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
      SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 560);
      sampler.start(hwnd);
      startTick = GetTickCount64();
      startCpu = cpuTime();
      if (benchmark)
        SetTimer(hwnd, 2, 30000, nullptr);
      if (verify)
        SetTimer(hwnd, 3, 3500, nullptr);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      auto dc = BeginPaint(hwnd, &ps);
      paint(dc);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case sampleMessage:
      if (fullBenchmark && !historySeeded) {
        std::lock_guard lock(state.mutex);
        if (state.count) {
          auto seed = state.latest();
          for (size_t i = 0; i < capacity; i++) {
            state.ring[i] = seed;
            state.ring[i].tick = seed.tick - (capacity - 1 - i) * 1000;
            state.ring[i].wall = seed.wall - (capacity - 1 - i) * 10000000ull;
          }
          state.head = 0;
          state.count = capacity;
          historySeeded = true;
        }
      }
      if (config.desktop && host && !IsWindow(host)) {
        host = nullptr;
        attach();
      }
      if (!adjusting && lastForeground != GetForegroundWindow()) {
        lastForeground = GetForegroundWindow();
        lowerWindow();
      }
      if (!paused && IsWindowVisible(hwnd) && !IsIconic(hwnd))
        InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_SIZE:
      scroll = std::clamp(scroll, 0, std::max(0, contentHeight - int(HIWORD(lp))));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL:
      return 0;
    case WM_LBUTTONDOWN: {
      int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
      RECT c;
      GetClientRect(hwnd, &c);
      if (!adjusting) {
        if (x >= 16 && x <= 44 && y >= 12 && y <= 40)
          beginAdjust();
        return 0;
      }
      POINT point{x, y};
      if (PtInRect(&metricControl, point)) {
        metricPopup();
        return 0;
      }
      if (PtInRect(&fontControl, point)) {
        fontPopup();
        return 0;
      }
      if (menuDragAt(point)) {
        movingWindow = true;
        GetCursorPos(&moveStart);
        GetWindowRect(hwnd, &moveOriginal);
        SetCapture(hwnd);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return 0;
      }
      for (int i = 0; i < 3; i++)
        if (PtInRect(&controlTracks[i], point)) {
          draggingControl = i + 1;
          SetCapture(hwnd);
          changeControl(draggingControl, x);
          return 0;
        }
      for (int i = 0; i < 3; i++)
        if (PtInRect(&summaryHandles[i], point)) {
          draggingSummary = i;
          dragStartY = y;
          dragStartHeight = i == 0 ? config.cpuHeight : config.rankingHeight;
          SetCapture(hwnd);
          SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
          return 0;
        }
      if (y >= c.bottom - editorHeight && y < c.bottom - editorHeight + 40) {
        int step = std::min(76, int(c.right - 24) / 6), i = x >= 12 ? (x - 12) / step : -1;
        if (i == 0) {
          ReleaseCapture();
          SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        } else if (i >= 1 && i <= 3)
          command(i + 1);
        else if (i == 4)
          command(6);
        else if (i == 5)
          command(9);
      }
      return 0;
    }
    case WM_MOUSEACTIVATE:
      return adjusting ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_SIZING: {
      auto r = (RECT *)lp;
      RECT currentClient, currentOuter;
      GetClientRect(hwnd, &currentClient);
      GetWindowRect(hwnd, &currentOuter);
      int borderW = currentOuter.right - currentOuter.left - currentClient.right;
      int borderH = currentOuter.bottom - currentOuter.top - currentClient.bottom;
      int need =
          minimumHeight(r->right - r->left - borderW) + (adjusting ? editorHeight : 0) + borderH;
      if (r->bottom - r->top < need) {
        if (wp == WMSZ_TOP || wp == WMSZ_TOPLEFT || wp == WMSZ_TOPRIGHT)
          r->top = r->bottom - need;
        else
          r->bottom = r->top + need;
      }
      return TRUE;
    }
    case WM_LBUTTONUP:
      if (movingWindow) {
        movingWindow = false;
        ReleaseCapture();
        save();
        return 0;
      }
      if (draggingControl) {
        draggingControl = 0;
        ReleaseCapture();
        save();
        return 0;
      }
      if (draggingSummary >= 0) {
        draggingSummary = -1;
        ReleaseCapture();
        save();
        return 0;
      }
      return 0;
    case WM_CAPTURECHANGED:
      movingWindow = false;
      draggingControl = 0;
      draggingSummary = -1;
      return 0;
    case WM_SETCURSOR:
      if (adjusting && LOWORD(lp) == HTCLIENT) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        if (movingWindow || menuDragAt(p)) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
          return TRUE;
        }
        if (draggingSummary >= 0 || PtInRect(&summaryHandles[0], p) ||
            PtInRect(&summaryHandles[2], p)) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
          return TRUE;
        }
      }
      break;
    case WM_MOUSEMOVE:
      if (movingWindow) {
        POINT current;
        GetCursorPos(&current);
        moveFromMenu(current);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return 0;
      }
      if (draggingControl) {
        changeControl(draggingControl, GET_X_LPARAM(lp));
        return 0;
      }
      if (draggingSummary >= 0) {
        RECT c;
        GetClientRect(hwnd, &c);
        int room = int(c.bottom) - editorHeight - 8 - 16 - 23 - 10 - rowsFor(c.right) * 56 -
                   groupSpacing() - (c.right < 650 ? 36 : 0);
        if (draggingSummary == 0) {
          int maximum = std::clamp((room - config.rankingHeight) / 2, 80, 400);
          int desired = std::clamp(dragStartHeight + GET_Y_LPARAM(lp) - dragStartY, 80, maximum);
          config.cpuHeight = config.memoryHeight = desired;
        } else {
          int maximum = std::clamp(room - 2 * config.cpuHeight, 200, 500);
          config.rankingHeight =
              std::clamp(dragStartHeight + dragStartY - GET_Y_LPARAM(lp), 200, maximum);
        }
        SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (adjusting)
        hover(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
      return 0;
    case WM_CONTEXTMENU:
      if (adjusting)
        popup();
      return 0;
    case trayMessage:
      if (lp == WM_RBUTTONUP)
        popup();
      else if (lp == WM_LBUTTONDBLCLK)
        command(1);
      return 0;
    case WM_COMMAND:
      command(LOWORD(wp));
      return 0;
    case WM_NCHITTEST: {
      LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &p);
      RECT c;
      GetClientRect(hwnd, &c);
      if (r == HTCLIENT && (!config.desktop || adjusting) && (!config.through || adjusting) &&
          p.x > c.right - 12 && p.y > c.bottom - 12)
        return HTBOTTOMRIGHT;
      return r;
    }
    case WM_GETMINMAXINFO: {
      auto p = (MINMAXINFO *)lp;
      p->ptMinTrackSize = {380, 280};
      p->ptMaxTrackSize = {3840, 2160};
      return 0;
    }
    case WM_TIMER:
      if (wp == 3) {
        KillTimer(hwnd, 3);
        beginAdjust();
        SetWindowPos(hwnd, nullptr, 0, 0, 920, 1100, SWP_NOMOVE | SWP_NOZORDER);
        config.cpuHeight = 80;
        config.memoryHeight = 80;
        config.rankingHeight = 240;
        capture(basePath() + L"\\summary-before.bmp");
        int hy = summaryHandles[0].top + 4;
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, hy));
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(200, hy + 50));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(200, hy + 50));
        bool independentCpu = config.cpuHeight == 130 && config.memoryHeight == 130;
        capture(basePath() + L"\\summary-cpu.bmp");
        hy = summaryHandles[2].top + 4;
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, hy));
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(200, hy - 60));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(200, hy - 60));
        bool independentMemory =
            config.cpuHeight == 130 && config.memoryHeight == 130 && config.rankingHeight == 300;
        bool noScroll = (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VSCROLL) == 0;
        config.seconds = 20;
        command(4);
        bool times = config.seconds == 40;
        command(4);
        times &= config.seconds == 60;
        command(4);
        times &= config.seconds == 20;
        config.opacityPercent = config.cardOpacityPercent = 100;
        changeControl(1, controlTracks[0].right);
        auto checkAlpha = [&] {
          HDC target = GetDC(hwnd);
          paint(target);
          ReleaseDC(hwnd, target);
          if (!layeredPresented || layeredError || !alphaPixels)
            return false;
          RECT wr;
          GetWindowRect(hwnd, &wr);
          POINT p{0, 0};
          ClientToScreen(hwnd, &p);
          int ox = p.x - wr.left, oy = p.y - wr.top;
          int backgrounds = 0, cards = 0, data = 0;
          for (int y = 0; y < bodyHeight(); y++)
            for (int x = 0; x < bitmapW; x++) {
              uint32_t rgb = bitmapPixels[size_t(y) * bitmapW + x] & 0xffffff;
              unsigned expected = 255;
              if (rgb == pixelColor(bg) || rgb == pixelColor(borderColor())) {
                expected = (config.opacityPercent * 255 + 50) / 100;
                backgrounds++;
              } else if (rgb == pixelColor(card)) {
                expected = (config.cardOpacityPercent * 255 + 50) / 100;
                cards++;
              } else
                data++;
              if ((alphaPixels[size_t(y + oy) * alphaW + x + ox] >> 24) != expected)
                return false;
            }
          return backgrounds > 0 && cards > 0 && data > 0;
        };
        bool transparencyOk =
            config.opacityPercent == 20 && config.cardOpacityPercent == 100 && checkAlpha();
        changeControl(2, controlTracks[1].right);
        changeControl(1, controlTracks[0].left);
        transparencyOk &=
            config.opacityPercent == 100 && config.cardOpacityPercent == 20 && checkAlpha();
        config.opacityPercent = 35;
        config.cardOpacityPercent = 70;
        applyTransparency();
        transparencyOk &= checkAlpha();
        capture(basePath() + L"\\transparency-preview.bmp", true);
        confirmAdjust();
        transparencyOk &= checkAlpha();
        RECT alphaClient;
        GetClientRect(hwnd, &alphaClient);
        POINT alphaOrigin{0, 0};
        ClientToScreen(hwnd, &alphaOrigin);
        beginAdjust();
        confirmAdjust();
        RECT alphaAfter;
        GetClientRect(hwnd, &alphaAfter);
        POINT alphaPoint{0, 0};
        ClientToScreen(hwnd, &alphaPoint);
        transparencyOk &= checkAlpha() && EqualRect(&alphaClient, &alphaAfter) &&
                          alphaOrigin.x == alphaPoint.x && alphaOrigin.y == alphaPoint.y;
        beginAdjust();
        command(12);
        transparencyOk &= config.opacityPercent == 100 && config.cardOpacityPercent == 100 &&
                          !alphaDC && !(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED);
        bool familyOk = true;
        for (int i = 0; i < 7; i++) {
          chooseFont(i);
          LOGFONTW requested{};
          GetObjectW(font, sizeof(requested), &requested);
          familyOk &= wcscmp(requested.lfFaceName, fontNames[i]) == 0;
        }
        chooseFont(0);
        capture(basePath() + L"\\font-dengxian.bmp");
        chooseFont(2);
        changeControl(3, controlTracks[2].right);
        LOGFONTW lf{};
        GetObjectW(font, sizeof(lf), &lf);
        bool fontOk = familyOk && config.fontSize == 22 && lf.lfHeight == -22;
        capture(basePath() + L"\\large-font.bmp");
        SetWindowPos(hwnd, nullptr, 0, 0, 400, minimumHeight(400) + 40, SWP_NOMOVE | SWP_NOZORDER);
        capture(basePath() + L"\\large-font-narrow.bmp");
        SetWindowPos(hwnd, nullptr, 0, 0, 920, 1100, SWP_NOMOVE | SWP_NOZORDER);
        config.fontSize = 14;
        updateFonts();
        capture(basePath() + L"\\compact-edit.bmp");
        auto oldHits = hits;
        RECT oldClient;
        GetClientRect(hwnd, &oldClient);
        POINT oldOrigin{0, 0};
        ClientToScreen(hwnd, &oldOrigin);
        confirmAdjust();
        capture(basePath() + L"\\compact-fixed.bmp");
        bool previousDark = config.dark;
        config.dark = false;
        theme();
        capture(basePath() + L"\\visual-light.bmp");
        config.dark = previousDark;
        theme();
        RECT fixedClient;
        GetClientRect(hwnd, &fixedClient);
        POINT fixedOrigin{0, 0};
        ClientToScreen(hwnd, &fixedOrigin);
        bool stableGeometry = oldClient.right == fixedClient.right &&
                              oldClient.bottom - editorHeight == fixedClient.bottom &&
                              oldOrigin.x == fixedOrigin.x && oldOrigin.y == fixedOrigin.y &&
                              oldHits.size() == hits.size();
        for (size_t i = 0; stableGeometry && i < hits.size(); i++)
          stableGeometry = EqualRect(&oldHits[i].rect, &hits[i].rect);

        RECT er;
        GetClientRect(hwnd, &er);
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        POINT editPoint{30, 26};
        ClientToScreen(hwnd, &editPoint);
        bool exposed = WindowFromPoint(editPoint) == hwnd;
        RECT r;
        GetClientRect(hwnd, &r);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 26));
        bool edit = adjusting;
        bool repeatStable = true;
        for (int cycle = 0; cycle < 3; cycle++) {
          confirmAdjust();
          beginAdjust();
          RECT now;
          GetClientRect(hwnd, &now);
          POINT origin{0, 0};
          ClientToScreen(hwnd, &origin);
          repeatStable &= now.right == oldClient.right && now.bottom == oldClient.bottom &&
                          origin.x == oldOrigin.x && origin.y == oldOrigin.y;
        }

        SetWindowPos(hwnd, nullptr, 0, 0, 400, minimumHeight(400) + 40, SWP_NOMOVE | SWP_NOZORDER);
        capture(basePath() + L"\\compact-narrow.bmp");
        bool groupsOk = true;
        RECT testClient;
        GetClientRect(hwnd, &testClient);
        POINT testOrigin{0, 0};
        ClientToScreen(hwnd, &testOrigin);
        for (int width : {380, 400, 535, 550, 650, 704, 875, 920, 1044, 1220, 1400, 1600}) {
          placeClient(testOrigin, width, minimumHeight(width) + editorHeight);
          HDC dc = GetDC(hwnd);
          paint(dc);
          ReleaseDC(hwnd, dc);
          int pBottom = INT_MIN, eTop = INT_MAX, count = 0;
          for (auto &hit : hits)
            if (hit.kind == 2) {
              count++;
              if (state.cores[hit.index].kind == L'P')
                pBottom = std::max(pBottom, int(hit.rect.bottom));
              if (state.cores[hit.index].kind == L'E')
                eTop = std::min(eTop, int(hit.rect.top));
            }
          groupsOk &= count == int(state.cores.size());
          if (pBottom != INT_MIN && eTop != INT_MAX)
            groupsOk &= eTop - pBottom >= 18;
          for (size_t i = 0; i < hits.size(); i++)
            for (size_t j = i + 1; j < hits.size(); j++)
              if (hits[i].kind == 2 && hits[j].kind == 2) {
                RECT overlap;
                groupsOk &= !IntersectRect(&overlap, &hits[i].rect, &hits[j].rect);
              }
        }
        placeClient(testOrigin, 920, minimumHeight(920) + editorHeight);
        HDC dc = GetDC(hwnd);
        paint(dc);
        ReleaseDC(hwnd, dc);
        RECT client;
        GetClientRect(hwnd, &client);
        int menuY = client.bottom - editorHeight;
        bool menuSafe = menuDragAt({client.right - 20, menuY + 20}) &&
                        !menuDragAt({110, menuY + 20}) && !menuDragAt({200, menuY + 56});
        RECT original;
        GetWindowRect(hwnd, &original);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(client.right - 20, menuY + 20));
        bool menuDrag = movingWindow;
        moveFromMenu({moveStart.x + 17, moveStart.y + 23});
        RECT moved;
        GetWindowRect(hwnd, &moved);
        menuDrag &= moved.left == original.left + 17 && moved.top == original.top + 23;
        SendMessageW(hwnd, WM_LBUTTONUP, 0, 0);
        placeClient(testOrigin, client.right, client.bottom);
        capture(basePath() + L"\\group-spacing.bmp");
        std::ofstream f{std::filesystem::path(basePath() + L"\\compact-check.json")};
        f << "{\"no_scroll\":" << noScroll << ",\"time_cycle\":" << times
          << ",\"edit_button\":" << edit << ",\"edit_button_exposed\":" << exposed
          << ",\"synchronized_summary_drag\":" << independentCpu
          << ",\"ranking_resize\":" << independentMemory
          << ",\"transparency_modes\":" << transparencyOk << ",\"global_font\":" << fontOk
          << ",\"fixed_geometry_unchanged\":" << stableGeometry
          << ",\"repeated_toggle_stable\":" << repeatStable
          << ",\"responsive_group_spacing\":" << groupsOk << ",\"menu_drag\":" << menuDrag
          << ",\"menu_controls_excluded\":" << menuSafe << "}";
        f.flush();
        RECT exitClient;
        GetClientRect(hwnd, &exitClient);
        int exitStep = std::min(76, int(exitClient.right - 24) / 6);
        SendMessageW(
            hwnd, WM_LBUTTONDOWN, MK_LBUTTON,
            MAKELPARAM(12 + 5 * exitStep + exitStep / 2, exitClient.bottom - editorHeight + 20));
        return 0;
      }
      if (wp == 2) {
        KillTimer(hwnd, 2);
        PROCESS_MEMORY_COUNTERS_EX pm{};
        GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof(pm));
        int logical = 0;
        for (auto &c : state.cores)
          logical += (int)c.threads.size();
        double cpu = (cpuTime() - startCpu) / 1e7 / ((GetTickCount64() - startTick) / 1000.) /
                     logical * 100.;
        {
          std::lock_guard lock(state.mutex);
          report(state,
                 basePath() +
                     (fullBenchmark ? L"\\native-benchmark-full.json" : L"\\native-benchmark.json"),
                 cpu, pm.WorkingSetSize / 1048576., pm.PrivateUsage / 1048576.);
        }
        capture(basePath() + L"\\native-dark.bmp");
        config.dark = false;
        theme();
        capture(basePath() + L"\\native-light.bmp");
        config.dark = true;
        theme();
        DestroyWindow(hwnd);
      }
      return 0;
    case WM_CLOSE:
      hideWindow();
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      hideWindow();
      if (!benchmark && !verify)
        save();
      sampler.stop();
      Shell_NotifyIconW(NIM_DELETE, &tray);
      tray.hWnd = nullptr;
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
};
} // namespace ct
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int) {
  using namespace ct;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
  InitCommonControlsEx(&ic);
  try {
    UI ui;
    ui.benchmark = wcsstr(command, L"--benchmark") != nullptr;
    ui.verify = wcsstr(command, L"--verify-ui") != nullptr;
    ui.fullBenchmark = wcsstr(command, L"--benchmark-full") != nullptr;
    if (ui.fullBenchmark)
      ui.config.seconds = 60;
    if (wcsstr(command, L"--benchmark-alpha")) {
      ui.config.opacityPercent = 35;
      ui.config.cardOpacityPercent = 70;
    }
    if (wcsstr(command, L"--diagnose")) {
      ui.sampler.start(nullptr);
      Sleep(6500);
      ui.sampler.stop();
      report(ui.state, basePath() + L"\\native-diagnostics.json");
      return 0;
    }
    if (ui.benchmark || ui.verify) {
      ui.config.desktop = false;
      ui.config.through = false;
    }
    auto mutex = CreateMutexW(nullptr, FALSE, L"Local\\CoreTraceNative");
    if (GetLastError() == ERROR_ALREADY_EXISTS && !ui.benchmark && !ui.verify) {
      CloseHandle(mutex);
      return 0;
    }
    WNDCLASSW cls{};
    cls.lpfnWndProc = UI::procedure;
    cls.hInstance = instance;
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    cls.lpszClassName = L"CoreTraceNative";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    RECT desktop;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop, 0);
    if (ui.config.x >
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) - 50 ||
        ui.config.y >
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) - 50) {
      ui.config.x = desktop.left + 20;
      ui.config.y = desktop.top + 20;
    }
    ui.config.height = std::max(ui.config.height, ui.minimumHeight(ui.config.width));
    auto w = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"CoreTrace", WS_POPUP,
                             ui.config.x, ui.config.y, ui.config.width, ui.config.height, nullptr,
                             nullptr, instance, &ui);
    if (!w)
      throw std::runtime_error("CreateWindow failed");
    ShowWindow(w, SW_SHOWNOACTIVATE);
    if (wcsstr(command, L"--adjust"))
      ui.beginAdjust();
    ui.attach();
    ui.through();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    CloseHandle(mutex);
    return 0;
  } catch (const std::exception &e) {
    MessageBoxA(nullptr, e.what(), "CoreTrace error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
