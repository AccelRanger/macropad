//  Build (MinGW-w64):
//    g++ -O2 -std=c++17 macropad_pc.cpp -o macropad_pc.exe -mwindows -static
//        -lgdiplus -lgdi32 -ladvapi32 -lole32 -loleaut32 -luuid -lcomdlg32
// ============================================================================

#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace Gdiplus;

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

// ----------------------------------------------------------------------------
// Constants / IDs
// ----------------------------------------------------------------------------
static const int  KEYS = 9;          // 3x3
static const DWORD BAUD = 115200;

enum {
  ID_PORT = 100, ID_REFRESH, ID_CONNECT,
  ID_PRESET, ID_NAME, ID_NEW, ID_RENAME, ID_DELETE,
  ID_KEY0 = 200  // 200..208
};
enum {
  WM_KEYPRESSED = WM_APP + 1,  // wParam = 1..9 (from the pad)
  WM_REC_DONE,                 // key recording finished
  WM_REC_CANCEL,               // Esc pressed while recording
  WM_LINK_LOST                 // serial port vanished
};

// ----------------------------------------------------------------------------
// Data model
// ----------------------------------------------------------------------------
typedef std::vector<WORD> Binding;

struct Preset {
  std::wstring name;
  Binding keys[KEYS];
  std::wstring cmd[KEYS];   // if non-empty, this key runs a command instead
};

static std::vector<Preset> g_presets;
static int          g_cur = 0;
static std::wstring g_port;

// ----------------------------------------------------------------------------
// Globals (UI + state)
// ----------------------------------------------------------------------------
enum WKind { W_BTN, W_SEL, W_FIELD, W_KEY };
struct Widget {
  int id;
  WKind kind;
  RECT r;
};

static HWND g_hwnd, g_nameEdit;
static HFONT g_editFont;
static HBRUSH g_editBrush;
static std::vector<Widget> g_w;
static std::wstring g_statusText;
static float g_s = 1.0f;          // DPI scale
static bool  g_editFocus = false;
static int   g_hover = -1, g_down = -1, g_flash = -1;
static bool  g_tracking = false;
static Font *g_fTitle, *g_fUI, *g_fSmall, *g_fNum, *g_fKey;

static HANDLE g_com = INVALID_HANDLE_VALUE;
static std::thread g_thread;
static std::atomic<bool> g_run(false);

// recording state
static int     g_recIdx = -1;
static HHOOK   g_hook = nullptr;
static Binding g_held, g_combo, g_result;
static bool    g_recDone = false;

static int S(int v) { return (int)(v * g_s + 0.5f); }

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
static std::string u8(const std::wstring& s) {
  if (s.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string r(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n, nullptr, nullptr);
  return r;
}
static std::wstring w16(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring r(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &r[0], n);
  return r;
}
static std::wstring trim(std::wstring s) {
  while (!s.empty() && iswspace(s.back())) s.pop_back();
  size_t i = 0;
  while (i < s.size() && iswspace(s[i])) i++;
  return s.substr(i);
}
static std::wstring cleanName(std::wstring s) {
  std::wstring r;
  for (wchar_t c : s)
    if (c != L'[' && c != L']' && c != L'\r' && c != L'\n') r += c;
  return trim(r);
}

static bool isModifier(WORD code) {
  switch (code & 0xFF) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_LWIN: case VK_RWIN:
      return true;
  }
  return false;
}

static std::wstring keyName(WORD code) {
  WORD vk = code & 0xFF;
  switch (vk) {
    case VK_LCONTROL: case VK_CONTROL: return L"Ctrl";
    case VK_RCONTROL: return L"Right Ctrl";
    case VK_LSHIFT: case VK_SHIFT: return L"Shift";
    case VK_RSHIFT: return L"Right Shift";
    case VK_LMENU: case VK_MENU: return L"Alt";
    case VK_RMENU: return L"Right Alt";
    case VK_LWIN: return L"Win";
    case VK_RWIN: return L"Right Win";
    case VK_MEDIA_PLAY_PAUSE: return L"Play/Pause";
    case VK_MEDIA_NEXT_TRACK: return L"Next Track";
    case VK_MEDIA_PREV_TRACK: return L"Prev Track";
    case VK_MEDIA_STOP: return L"Media Stop";
    case VK_VOLUME_UP: return L"Volume Up";
    case VK_VOLUME_DOWN: return L"Volume Down";
    case VK_VOLUME_MUTE: return L"Mute";
  }
  UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  LONG lp = (LONG)(sc << 16) | ((code & 0x100) ? (1 << 24) : 0);
  wchar_t buf[64] = {0};
  if (GetKeyNameTextW(lp, buf, 64) > 0) return buf;
  return L"VK" + std::to_wstring(vk);
}

static std::wstring bindingText(const Binding& b) {
  if (b.empty()) return L"(unbound)";
  std::wstring s;
  for (size_t i = 0; i < b.size(); i++) {
    if (i) s += L" + ";
    s += keyName(b[i]);
  }
  return s;
}

// ----------------------------------------------------------------------------
// Persistence: %APPDATA%\MacropadStudio\presets.txt
// ----------------------------------------------------------------------------
static std::wstring dataPath() {
  wchar_t base[MAX_PATH] = {0};
  GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
  std::wstring dir = std::wstring(base) + L"\\MacropadStudio";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir + L"\\presets.txt";
}

static void saveAll() {
  std::string out;
  out += "port=" + u8(g_port) + "\n";
  out += "active=" + u8(g_presets[g_cur].name) + "\n";
  for (const Preset& p : g_presets) {
    out += "[" + u8(p.name) + "]\n";
    for (int i = 0; i < KEYS; i++) {
      out += std::to_string(i + 1) + "=";
      for (size_t j = 0; j < p.keys[i].size(); j++) {
        if (j) out += ",";
        out += std::to_string(p.keys[i][j]);
      }
      out += "\n";
      if (!p.cmd[i].empty()) out += "cmd" + std::to_string(i + 1) + "=" + u8(p.cmd[i]) + "\n";
    }
  }
  FILE* f = _wfopen(dataPath().c_str(), L"wb");
  if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
}

static void loadAll() {
  g_presets.clear();
  std::string data;
  FILE* f = _wfopen(dataPath().c_str(), L"rb");
  if (f) {
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.append(buf, n);
    fclose(f);
  }
  std::wstring active;
  Preset* cur = nullptr;
  size_t pos = 0;
  while (pos < data.size()) {
    size_t e = data.find('\n', pos);
    if (e == std::string::npos) e = data.size();
    std::string line = data.substr(pos, e - pos);
    pos = e + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty()) continue;

    if (line.front() == '[' && line.back() == ']') {
      g_presets.push_back(Preset());
      cur = &g_presets.back();
      cur->name = w16(line.substr(1, line.size() - 2));
    } else if (line.rfind("port=", 0) == 0) {
      g_port = w16(line.substr(5));
    } else if (line.rfind("active=", 0) == 0) {
      active = w16(line.substr(7));
    } else if (cur && line.rfind("cmd", 0) == 0) {
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      int idx = atoi(line.substr(3, eq - 3).c_str()) - 1;
      if (idx >= 0 && idx < KEYS) cur->cmd[idx] = w16(line.substr(eq + 1));
    } else if (cur && isdigit((unsigned char)line[0])) {
      size_t eq = line.find('=');
      if (eq == std::string::npos) continue;
      int idx = atoi(line.substr(0, eq).c_str()) - 1;
      if (idx < 0 || idx >= KEYS) continue;
      std::string list = line.substr(eq + 1);
      size_t p = 0;
      while (p < list.size()) {
        size_t c = list.find(',', p);
        if (c == std::string::npos) c = list.size();
        int v = atoi(list.substr(p, c - p).c_str());
        if (v > 0) cur->keys[idx].push_back((WORD)v);
        p = c + 1;
      }
    }
  }
  if (g_presets.empty()) {
    Preset p;
    p.name = L"Default";
    g_presets.push_back(p);
  }
  g_cur = 0;
  for (size_t i = 0; i < g_presets.size(); i++)
    if (g_presets[i].name == active) g_cur = (int)i;
}

// ----------------------------------------------------------------------------
// UI refresh / ports
// ----------------------------------------------------------------------------
static void refreshGrid() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }
static void setStatus(const std::wstring& t) { g_statusText = t; refreshGrid(); }

static void refreshPresets() {
  SetWindowTextW(g_nameEdit, g_presets[g_cur].name.c_str());
  refreshGrid();
}

static std::vector<std::wstring> listPorts() {
  std::vector<std::wstring> out;
  HKEY k;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &k) == ERROR_SUCCESS) {
    for (DWORD i = 0;; i++) {
      wchar_t name[256]; DWORD nl = 256;
      wchar_t data[256] = {0}; DWORD dl = sizeof(data) - 2, type = 0;
      if (RegEnumValueW(k, i, name, &nl, nullptr, &type, (LPBYTE)data, &dl) != ERROR_SUCCESS) break;
      if (type == REG_SZ) out.push_back(data);
    }
    RegCloseKey(k);
  }
  std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return a < b;
  });
  return out;
}

// ----------------------------------------------------------------------------
// Serial
// ----------------------------------------------------------------------------
static void readerLoop(HANDLE h, HWND wnd) {
  std::string line;
  char buf[64];
  DWORD n;
  while (g_run) {
    if (!ReadFile(h, buf, sizeof buf, &n, nullptr)) {
      if (g_run) PostMessageW(wnd, WM_LINK_LOST, 0, 0);
      return;
    }
    for (DWORD i = 0; i < n; i++) {
      char c = buf[i];
      if (c == '\n') {
        int v = atoi(line.c_str());
        if (v >= 1 && v <= KEYS) PostMessageW(wnd, WM_KEYPRESSED, (WPARAM)v, 0);
        line.clear();
      } else if (c != '\r') {
        if (line.size() < 16) line += c;
      }
    }
  }
}

static void disconnectPort() {
  g_run = false;
  if (g_thread.joinable()) g_thread.join();
  if (g_com != INVALID_HANDLE_VALUE) CloseHandle(g_com);
  g_com = INVALID_HANDLE_VALUE;
}

static bool connectPort(const std::wstring& port) {
  if (port.empty()) { setStatus(L"No COM port selected."); return false; }
  std::wstring path = L"\\\\.\\" + port;
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    setStatus(L"Could not open " + port + L" (is another program using it?)");
    return false;
  }
  DCB dcb = {0};
  dcb.DCBlength = sizeof dcb;
  GetCommState(h, &dcb);
  dcb.BaudRate = BAUD;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;   // needed for ESP32 USB-CDC to transmit
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fOutX = dcb.fInX = FALSE;
  SetCommState(h, &dcb);
  COMMTIMEOUTS to = {50, 0, 100, 0, 0};   // reads return after <=100 ms
  SetCommTimeouts(h, &to);
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  g_com = h;
  g_run = true;
  g_thread = std::thread(readerLoop, h, g_hwnd);
  g_port = port;
  setStatus(L"Connected to " + port + L". Press a key on the macropad.");
  saveAll();
  return true;
}

// ----------------------------------------------------------------------------
// Sending keystrokes
// ----------------------------------------------------------------------------
static void sendBinding(const Binding& b) {
  if (b.empty()) return;
  std::vector<INPUT> in;
  auto add = [&](WORD code, bool up) {
    INPUT i = {};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = code & 0xFF;
    i.ki.wScan = (WORD)MapVirtualKeyW(code & 0xFF, MAPVK_VK_TO_VSC);
    i.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | ((code & 0x100) ? KEYEVENTF_EXTENDEDKEY : 0);
    in.push_back(i);
  };
  for (size_t i = 0; i < b.size(); i++) add(b[i], false);
  for (size_t i = b.size(); i-- > 0;) add(b[i], true);
  SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

// ----------------------------------------------------------------------------
// Key recording (low-level keyboard hook, only active while editing a bind)
// ----------------------------------------------------------------------------
static void finishRecording() {
  g_result = g_combo;
  g_recDone = true;
  PostMessageW(g_hwnd, WM_REC_DONE, 0, 0);
}

static LRESULT CALLBACK kbHook(int n, WPARAM wp, LPARAM lp) {
  if (n != HC_ACTION) return CallNextHookEx(nullptr, n, wp, lp);
  const KBDLLHOOKSTRUCT* k = (const KBDLLHOOKSTRUCT*)lp;
  if (g_recDone) return 1;

  bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
  bool up   = (wp == WM_KEYUP   || wp == WM_SYSKEYUP);
  WORD code = (WORD)(k->vkCode & 0xFF) | ((k->flags & LLKHF_EXTENDED) ? 0x100 : 0);

  if (down) {
    if (k->vkCode == VK_ESCAPE) {
      g_recDone = true;
      PostMessageW(g_hwnd, WM_REC_CANCEL, 0, 0);
      return 1;
    }
    if (std::find(g_held.begin(), g_held.end(), code) == g_held.end()) {
      g_held.push_back(code);
      if (std::find(g_combo.begin(), g_combo.end(), code) == g_combo.end())
        g_combo.push_back(code);
    }
    if (!isModifier(code)) finishRecording();     // shortcut / single key done
  } else if (up) {
    auto it = std::find(g_held.begin(), g_held.end(), code);
    if (it != g_held.end()) {
      g_held.erase(it);
      // only modifiers were pressed (e.g. just "Ctrl" or "Ctrl+Shift")
      if (g_held.empty() && !g_combo.empty()) finishRecording();
    }
  }
  return 1;  // swallow everything while recording
}

static void stopRecording() {
  if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
  g_recIdx = -1;
  g_recDone = false;
  refreshGrid();
}

static void startRecording(int idx) {
  if (g_recIdx >= 0) stopRecording();
  g_held.clear();
  g_combo.clear();
  g_recDone = false;
  g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, kbHook, GetModuleHandleW(nullptr), 0);
  if (!g_hook) { setStatus(L"Could not start key capture."); return; }
  g_recIdx = idx;
  refreshGrid();
  SetFocus(g_hwnd);
  setStatus(L"Editing key " + std::to_wstring(idx + 1) +
            L": press a key or a shortcut. Esc cancels.");
}

// ----------------------------------------------------------------------------
// Preset actions
// ----------------------------------------------------------------------------
static bool nameExists(const std::wstring& n, int ignore = -1) {
  for (size_t i = 0; i < g_presets.size(); i++)
    if ((int)i != ignore && _wcsicmp(g_presets[i].name.c_str(), n.c_str()) == 0) return true;
  return false;
}

static std::wstring editText() {
  wchar_t buf[128] = {0};
  GetWindowTextW(g_nameEdit, buf, 127);
  return cleanName(buf);
}

static void newPreset() {
  std::wstring name = editText();
  if (name.empty() || nameExists(name)) {
    int i = 1;
    do { name = L"Preset " + std::to_wstring(i++); } while (nameExists(name));
  }
  Preset p;
  p.name = name;
  g_presets.push_back(p);
  g_cur = (int)g_presets.size() - 1;
  refreshPresets();
  saveAll();
  setStatus(L"Created preset \"" + name + L"\".");
}

static void renamePreset() {
  std::wstring name = editText();
  if (name.empty()) { setStatus(L"Type a name first."); return; }
  if (nameExists(name, g_cur)) { setStatus(L"A preset with that name already exists."); return; }
  g_presets[g_cur].name = name;
  refreshPresets();
  saveAll();
  setStatus(L"Renamed preset.");
}

static void deletePreset() {
  if (g_presets.size() <= 1) { setStatus(L"You need at least one preset."); return; }
  std::wstring q = L"Delete preset \"" + g_presets[g_cur].name + L"\"?";
  if (MessageBoxW(g_hwnd, q.c_str(), L"Delete preset", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
  g_presets.erase(g_presets.begin() + g_cur);
  if (g_cur >= (int)g_presets.size()) g_cur = (int)g_presets.size() - 1;
  refreshPresets();
  saveAll();
}

enum { ID_D_EDIT = 300, ID_D_BROWSE, ID_D_SAVE, ID_D_CANCEL };
enum { CMD_RECORD = 1, CMD_COMMAND, CMD_CLEAR };

static Color C(int a, int r, int g, int b) {
  auto cl = [](int v) { return (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
  return Color(cl(a), cl(r), cl(g), cl(b));
}

// palette (dark, warm)
static Color cBg()      { return C(255, 38, 38, 36); }
static Color cCard()    { return C(255, 48, 48, 46); }
static Color cBorder()  { return C(255, 70, 69, 65); }
static Color cBorderHi(){ return C(255, 98, 96, 90); }
static Color cText()    { return C(255, 250, 249, 245); }
static Color cMuted()   { return C(255, 170, 168, 160); }
static Color cFaint()   { return C(255, 112, 110, 103); }
static Color cAccent()  { return C(255, 217, 119, 87); }
static Color cAccentHi(){ return C(255, 232, 138, 106); }
static Color cAccentBg(){ return C(255, 66, 47, 40); }
static Color cHover()   { return C(255, 58, 58, 55); }
static Color cPress()   { return C(255, 42, 42, 40); }
static Color cDanger()  { return C(255, 238, 112, 96); }
static Color cGreen()   { return C(255, 110, 190, 120); }

static void roundPath(GraphicsPath& p, RectF r, float rad) {
  float d = min(rad * 2.0f, min(r.Width, r.Height));
  p.AddArc(r.X, r.Y, d, d, 180, 90);
  p.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
  p.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
  p.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
  p.CloseFigure();
}

static void drawText(Graphics& g, const std::wstring& t, Font* f, RectF r, Color c,
                     StringAlignment h, StringAlignment v, bool wrap) {
  StringFormat sf;
  sf.SetAlignment(h);
  sf.SetLineAlignment(v);
  sf.SetTrimming(StringTrimmingEllipsisCharacter);
  if (!wrap) sf.SetFormatFlags(StringFormatFlagsNoWrap);
  SolidBrush b(c);
  g.DrawString(t.c_str(), -1, f, r, &sf, &b);
}

static void box(Graphics& g, RectF r, float rad, Color fill, Color border, float bw) {
  GraphicsPath p;
  roundPath(p, r, rad);
  SolidBrush fb(fill);
  g.FillPath(&fb, &p);
  Pen pn(border, bw);
  g.DrawPath(&pn, &p);
}

static bool connected() { return g_com != INVALID_HANDLE_VALUE; }

// dark title bar, dark popup menus, dark native controls
static void darkTitle(HWND h) {
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return;
  typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
  SetAttr f = (SetAttr)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
  BOOL on = TRUE;
  if (f) f(h, 20, &on, sizeof on);
}
static void darkTheme(HWND h, const wchar_t* theme) {
  HMODULE ux = LoadLibraryW(L"uxtheme.dll");
  if (!ux) return;
  typedef HRESULT(WINAPI * SetTheme)(HWND, LPCWSTR, LPCWSTR);
  SetTheme f = (SetTheme)(void*)GetProcAddress(ux, "SetWindowTheme");
  if (f) f(h, theme, nullptr);
}
static void enableDarkMenus() {
  HMODULE h = LoadLibraryW(L"uxtheme.dll");
  if (!h) return;
  typedef int(WINAPI * SetMode)(int);
  typedef void(WINAPI * Flush)();
  SetMode setMode = (SetMode)(void*)GetProcAddress(h, (LPCSTR)(ULONG_PTR)135);
  Flush flush = (Flush)(void*)GetProcAddress(h, (LPCSTR)(ULONG_PTR)136);
  if (setMode) setMode(2);  // ForceDark
  if (flush) flush();
}

static std::wstring widgetText(int id) {
  if (id == ID_PORT) return g_port.empty() ? L"Select port" : g_port;
  if (id == ID_CONNECT) return connected() ? L"Disconnect" : L"Connect";
  if (id == ID_PRESET) return g_presets[g_cur].name;
  if (id == ID_NEW) return L"New";
  if (id == ID_RENAME) return L"Rename";
  if (id == ID_DELETE) return L"Delete";
  return L"";
}

static void drawWidget(Graphics& g, int i) {
  const Widget& w = g_w[i];
  RectF r((REAL)w.r.left, (REAL)w.r.top, (REAL)(w.r.right - w.r.left), (REAL)(w.r.bottom - w.r.top));
  bool hov = (i == g_hover);
  bool dn = (i == g_down && hov);

  if (w.kind == W_KEY) {
    int idx = w.id - ID_KEY0;
    bool rec = (idx == g_recIdx);
    bool fl = (idx == g_flash);
    float rad = 14.0f * g_s;
    if (rec || fl) box(g, r, rad, cAccentBg(), cAccent(), 2.0f * g_s);
    else box(g, r, rad, dn ? cPress() : (hov ? C(255, 54, 54, 51) : cCard()), hov ? cBorderHi() : cBorder(), 1.0f * g_s);

    drawText(g, L"KEY " + std::to_wstring(idx + 1), g_fNum, RectF(r.X + 14 * g_s, r.Y + 10 * g_s, 70 * g_s, 16 * g_s),
             cFaint(), StringAlignmentNear, StringAlignmentNear, false);

    const Preset& p = g_presets[g_cur];
    bool isCmd = !p.cmd[idx].empty();
    if (isCmd && !rec)
      drawText(g, L"COMMAND", g_fNum, RectF(r.X + r.Width - 94 * g_s, r.Y + 10 * g_s, 80 * g_s, 16 * g_s),
               cAccent(), StringAlignmentFar, StringAlignmentNear, false);

    RectF body(r.X + 14 * g_s, r.Y + 28 * g_s, r.Width - 28 * g_s, r.Height - 36 * g_s);
    if (rec) {
      drawText(g, L"Press a key or shortcut", g_fKey, body, cAccent(), StringAlignmentNear, StringAlignmentNear, true);
      drawText(g, L"Esc to cancel", g_fSmall, RectF(r.X + 14 * g_s, r.Y + r.Height - 26 * g_s, r.Width - 28 * g_s, 18 * g_s),
               cMuted(), StringAlignmentNear, StringAlignmentCenter, false);
    } else if (isCmd) {
      drawText(g, p.cmd[idx], g_fKey, body, cText(), StringAlignmentNear, StringAlignmentNear, true);
    } else if (p.keys[idx].empty()) {
      drawText(g, L"Unbound", g_fKey, body, cFaint(), StringAlignmentNear, StringAlignmentNear, true);
    } else {
      drawText(g, bindingText(p.keys[idx]), g_fKey, body, cText(), StringAlignmentNear, StringAlignmentNear, true);
    }
    return;
  }

  float rad = 10.0f * g_s;
  if (w.kind == W_FIELD) {
    box(g, r, rad, cCard(), g_editFocus ? cAccent() : cBorder(), g_editFocus ? 1.6f * g_s : 1.0f * g_s);
    return;
  }

  bool primary = (w.id == ID_CONNECT && !connected());
  Color fill, border, tcol = cText();
  if (primary) {
    fill = dn ? C(255, 190, 100, 70) : (hov ? cAccentHi() : cAccent());
    border = fill;
    tcol = C(255, 38, 24, 18);
  } else {
    fill = dn ? cPress() : (hov ? cHover() : cCard());
    border = hov ? cBorderHi() : cBorder();
    if (w.id == ID_DELETE) tcol = cDanger();
  }
  box(g, r, rad, fill, border, 1.0f * g_s);

  if (w.kind == W_BTN) {
    drawText(g, widgetText(w.id), g_fUI, r, tcol, StringAlignmentCenter, StringAlignmentCenter, false);
  } else {  // W_SEL
    RectF lt(r.X + 14 * g_s, r.Y, r.Width - 40 * g_s, r.Height);
    drawText(g, widgetText(w.id), g_fUI, lt, tcol, StringAlignmentNear, StringAlignmentCenter, false);
    float cx = r.X + r.Width - 18 * g_s, cy = r.Y + r.Height / 2.0f;
    Pen cp(cMuted(), 1.6f * g_s);
    cp.SetStartCap(LineCapRound);
    cp.SetEndCap(LineCapRound);
    g.DrawLine(&cp, cx - 4.0f * g_s, cy - 2.0f * g_s, cx, cy + 2.2f * g_s);
    g.DrawLine(&cp, cx, cy + 2.2f * g_s, cx + 4.0f * g_s, cy - 2.0f * g_s);
  }
}

static void paintScene(Graphics& g, int W, int H) {
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintAntiAlias);
  g.Clear(cBg());

  drawText(g, L"Macropad config", g_fTitle, RectF(20.0f * g_s, 14.0f * g_s, 300.0f * g_s, 40.0f * g_s),
           cText(), StringAlignmentNear, StringAlignmentCenter, false);

  // connection indicator (top right)
  {
    float d = 9.0f * g_s;
    float cy = 34.0f * g_s;
    std::wstring t = connected() ? L"Connected" : L"Not connected";
    drawText(g, t, g_fSmall, RectF(W - 20 * g_s - 120 * g_s, cy - 10 * g_s, 120 * g_s, 20 * g_s),
             cMuted(), StringAlignmentFar, StringAlignmentCenter, false);
    SolidBrush db(connected() ? cGreen() : cFaint());
    g.FillEllipse(&db, W - 20 * g_s - 120 * g_s - 16 * g_s - d / 2, cy - d / 2, d, d);
  }

  for (size_t i = 0; i < g_w.size(); i++) drawWidget(g, (int)i);

  drawText(g, g_statusText, g_fUI, RectF(20.0f * g_s, 456.0f * g_s, (W - 40 * g_s), 24.0f * g_s),
           cText(), StringAlignmentNear, StringAlignmentCenter, false);
  drawText(g, L"Click a tile to record a shortcut   |   Right-click for command, program or clear   |   Esc cancels",
           g_fSmall, RectF(20.0f * g_s, 482.0f * g_s, (W - 40 * g_s), 20.0f * g_s),
           cMuted(), StringAlignmentNear, StringAlignmentCenter, false);
}

static void paintAll(HDC hdc, int W, int H) {
  if (W <= 0 || H <= 0) return;
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = W;
  bi.bmiHeader.biHeight = -H;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib) return;
  HDC mem = CreateCompatibleDC(hdc);
  HGDIOBJ old = SelectObject(mem, dib);
  {
    Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)bits);
    Graphics g(&bmp);
    paintScene(g, W, H);
  }
  BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
  SelectObject(mem, old);
  DeleteDC(mem);
  DeleteObject(dib);
}

// ----------------------------------------------------------------------------
// Layout
// ----------------------------------------------------------------------------
static const int CLIENT_W = 560, CLIENT_H = 512;

static RECT R(int l, int t, int r, int b) {
  RECT x = {S(l), S(t), S(r), S(b)};
  return x;
}

static void layout() {
  g_w.clear();
  g_w.push_back({ID_PORT,    W_SEL, R(20, 68, 230, 102)});
  g_w.push_back({ID_CONNECT, W_BTN, R(240, 68, 350, 102)});
  g_w.push_back({ID_PRESET,  W_SEL, R(20, 112, 190, 146)});
  g_w.push_back({ID_NAME,    W_FIELD, R(198, 112, 338, 146)});
  g_w.push_back({ID_NEW,     W_BTN, R(346, 112, 398, 146)});
  g_w.push_back({ID_RENAME,  W_BTN, R(406, 112, 466, 146)});
  g_w.push_back({ID_DELETE,  W_BTN, R(474, 112, 540, 146)});
  for (int i = 0; i < KEYS; i++) {
    int r = i / 3, c = i % 3;
    int x = 20 + c * (164 + 14), y = 164 + r * (84 + 12);
    g_w.push_back({ID_KEY0 + i, W_KEY, R(x, y, x + 164, y + 84)});
  }
}

static int hitTest(int x, int y) {
  POINT p = {x, y};
  for (size_t i = 0; i < g_w.size(); i++)
    if (g_w[i].kind != W_FIELD && PtInRect(&g_w[i].r, p)) return (int)i;
  return -1;
}

// ----------------------------------------------------------------------------
// Running a command (through cmd.exe, so "notepad", "C:\x\app.exe", "bash -c ...",
// "start https://..." and so on all work)
// ----------------------------------------------------------------------------
static void runCommand(const std::wstring& cmd) {
  std::wstring line = L"cmd.exe /s /c \"" + cmd + L"\"";
  std::vector<wchar_t> buf(line.begin(), line.end());
  buf.push_back(0);
  STARTUPINFOW si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  } else {
    setStatus(L"Could not run the command.");
  }
}

// ----------------------------------------------------------------------------
// Small modal dialog to enter a command / pick a program
// ----------------------------------------------------------------------------
static HWND g_dlgEdit = nullptr;
static bool g_dlgDone = false, g_dlgOk = false;
static std::wstring g_dlgText;
static HBRUSH g_dlgBrush = nullptr;

static void dlgSave() {
  int n = GetWindowTextLengthW(g_dlgEdit);
  std::vector<wchar_t> b(n + 2, 0);
  GetWindowTextW(g_dlgEdit, b.data(), n + 1);
  g_dlgText = trim(b.data());
  g_dlgOk = true;
  g_dlgDone = true;
}

static void dlgBrowse(HWND owner) {
  wchar_t file[MAX_PATH] = {0};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"Programs and scripts\0*.exe;*.bat;*.cmd;*.lnk;*.ps1;*.sh\0All files\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&ofn)) {
    std::wstring q = L"\"" + std::wstring(file) + L"\"";
    SetWindowTextW(g_dlgEdit, q.c_str());
    SendMessageW(g_dlgEdit, EM_SETSEL, q.size(), q.size());
  }
}

static LRESULT CALLBACK dlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case ID_D_SAVE: dlgSave(); return 0;
        case ID_D_CANCEL: g_dlgDone = true; return 0;
        case ID_D_BROWSE: dlgBrowse(h); return 0;
      }
      break;
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)w;
      SetBkColor(dc, RGB(38, 38, 36));
      SetTextColor(dc, RGB(250, 249, 245));
      return (LRESULT)g_dlgBrush;
    }
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)w;
      SetBkColor(dc, RGB(48, 48, 46));
      SetTextColor(dc, RGB(250, 249, 245));
      return (LRESULT)g_editBrush;
    }
    case WM_CLOSE:
      g_dlgDone = true;
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

static bool commandDialog(HWND owner, int keyNo, std::wstring& text) {
  int W = S(540), H = S(250);
  RECT orc;
  GetWindowRect(owner, &orc);
  int x = orc.left + ((orc.right - orc.left) - W) / 2;
  int y = orc.top + ((orc.bottom - orc.top) - H) / 2;
  RECT cr = {0, 0, W, H};
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  AdjustWindowRect(&cr, style, FALSE);

  std::wstring title = L"Command for key " + std::to_wstring(keyNo);
  HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MacropadCmd", title.c_str(), style, x, y,
                             cr.right - cr.left, cr.bottom - cr.top, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
  darkTitle(dlg);
  auto child = [&](const wchar_t* cls, const wchar_t* t, DWORD st, int l, int tp, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, t, WS_CHILD | WS_VISIBLE | st, S(l), S(tp), S(w), S(h), dlg,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_editFont, TRUE);
    if (!wcscmp(cls, L"BUTTON")) darkTheme(c, L"DarkMode_Explorer");
    else if (!wcscmp(cls, L"EDIT")) darkTheme(c, L"DarkMode_CFD");
    return c;
  };
  child(L"STATIC",
        L"Runs when this key is pressed, through cmd.exe. Examples:\n"
        L"    notepad\n"
        L"    \"C:\\Program Files\\App\\app.exe\" --flag\n"
        L"    start https://accelranger.accel-systems.xyz/",
        SS_LEFT, 20, 16, 500, 100, 0);
  g_dlgEdit = child(L"EDIT", text.c_str(), WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 20, 124, 400, 28, ID_D_EDIT);
  child(L"BUTTON", L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, 430, 123, 90, 30, ID_D_BROWSE);
  child(L"BUTTON", L"Save", WS_TABSTOP | BS_DEFPUSHBUTTON, 340, 196, 85, 32, ID_D_SAVE);
  child(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 435, 196, 85, 32, ID_D_CANCEL);

  g_dlgDone = false;
  g_dlgOk = false;
  EnableWindow(owner, FALSE);
  ShowWindow(dlg, SW_SHOW);
  SetFocus(g_dlgEdit);
  SendMessageW(g_dlgEdit, EM_SETSEL, 0, -1);

  MSG m;
  while (!g_dlgDone && GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (m.message == WM_KEYDOWN && (m.wParam == VK_RETURN || m.wParam == VK_ESCAPE) &&
        (m.hwnd == g_dlgEdit || GetParent(m.hwnd) == dlg)) {
      if (m.wParam == VK_RETURN) dlgSave(); else g_dlgDone = true;
      continue;
    }
    if (!IsDialogMessageW(dlg, &m)) {
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  EnableWindow(owner, TRUE);
  DestroyWindow(dlg);
  SetForegroundWindow(owner);
  if (g_dlgOk) text = g_dlgText;
  return g_dlgOk;
}

// ----------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------
static int popupList(const RECT& r, const std::vector<std::wstring>& items, int checked) {
  HMENU m = CreatePopupMenu();
  for (size_t i = 0; i < items.size(); i++)
    AppendMenuW(m, MF_STRING | ((int)i == checked ? MF_CHECKED : 0), 1000 + i, items[i].c_str());
  POINT p = {r.left, r.bottom + S(4)};
  ClientToScreen(g_hwnd, &p);
  int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, g_hwnd, nullptr);
  DestroyMenu(m);
  return cmd >= 1000 ? cmd - 1000 : -1;
}

static void keyContextMenu(int idx, POINT screenPt) {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, CMD_RECORD, L"Record shortcut");
  AppendMenuW(m, MF_STRING, CMD_COMMAND, L"Run command / open program...");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, CMD_CLEAR, L"Clear");
  int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, screenPt.x, screenPt.y, 0, g_hwnd, nullptr);
  DestroyMenu(m);

  Preset& p = g_presets[g_cur];
  if (cmd == CMD_RECORD) {
    startRecording(idx);
  } else if (cmd == CMD_COMMAND) {
    std::wstring text = p.cmd[idx];
    if (commandDialog(g_hwnd, idx + 1, text)) {
      p.cmd[idx] = text;
      if (!text.empty()) p.keys[idx].clear();
      saveAll();
      setStatus(text.empty() ? L"Command removed from key " + std::to_wstring(idx + 1) + L"."
                             : L"Key " + std::to_wstring(idx + 1) + L" runs: " + text);
    }
  } else if (cmd == CMD_CLEAR) {
    p.keys[idx].clear();
    p.cmd[idx].clear();
    saveAll();
    setStatus(L"Cleared key " + std::to_wstring(idx + 1) + L".");
  }
  refreshGrid();
}

static void onWidget(int i) {
  int id = g_w[i].id;
  if (id >= ID_KEY0 && id < ID_KEY0 + KEYS) { startRecording(id - ID_KEY0); return; }
  if (g_recIdx >= 0) stopRecording();

  switch (id) {
    case ID_PORT: {
      std::vector<std::wstring> ports = listPorts();
      if (ports.empty()) { setStatus(L"No COM ports found. Plug in the macropad and try again."); break; }
      int cur = -1;
      for (size_t k = 0; k < ports.size(); k++) if (ports[k] == g_port) cur = (int)k;
      int pick = popupList(g_w[i].r, ports, cur);
      if (pick >= 0) {
        if (connected()) disconnectPort();
        g_port = ports[pick];
        saveAll();
        setStatus(L"Port set to " + g_port + L". Press Connect.");
      }
      break;
    }
    case ID_CONNECT:
      if (connected()) {
        disconnectPort();
        setStatus(L"Disconnected.");
      } else {
        if (g_port.empty()) {
          std::vector<std::wstring> ports = listPorts();
          if (!ports.empty()) g_port = ports[0];
        }
        connectPort(g_port);
      }
      break;
    case ID_PRESET: {
      std::vector<std::wstring> names;
      for (const Preset& p : g_presets) names.push_back(p.name);
      int pick = popupList(g_w[i].r, names, g_cur);
      if (pick >= 0) {
        g_cur = pick;
        refreshPresets();
        saveAll();
        setStatus(L"Active preset: " + g_presets[g_cur].name);
      }
      break;
    }
    case ID_NEW:    newPreset(); break;
    case ID_RENAME: renamePreset(); break;
    case ID_DELETE: deletePreset(); break;
  }
  refreshGrid();
}

static void positionEdit() {
  for (const Widget& w : g_w) {
    if (w.id != ID_NAME) continue;
    int h = S(18);
    SetWindowPos(g_nameEdit, nullptr, w.r.left + S(12), w.r.top + ((w.r.bottom - w.r.top) - h) / 2,
                 (w.r.right - w.r.left) - S(24), h, SWP_NOZORDER);
  }
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_hwnd = hwnd;
      darkTitle(hwnd);

      g_fTitle = new Font(L"Georgia", 26.0f * g_s, FontStyleRegular, UnitPixel);
      g_fUI    = new Font(L"Segoe UI", 14.5f * g_s, FontStyleRegular, UnitPixel);
      g_fSmall = new Font(L"Segoe UI", 12.0f * g_s, FontStyleRegular, UnitPixel);
      g_fNum   = new Font(L"Segoe UI", 11.0f * g_s, FontStyleBold, UnitPixel);
      g_fKey   = new Font(L"Segoe UI", 15.5f * g_s, FontStyleRegular, UnitPixel);

      layout();
      g_editFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Segoe UI");
      g_editBrush = CreateSolidBrush(RGB(48, 48, 46));
      g_dlgBrush = CreateSolidBrush(RGB(38, 38, 36));
      g_nameEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd,
                                   (HMENU)(INT_PTR)ID_NAME, GetModuleHandleW(nullptr), nullptr);
      SendMessageW(g_nameEdit, WM_SETFONT, (WPARAM)g_editFont, TRUE);
      SendMessageW(g_nameEdit, EM_LIMITTEXT, 40, 0);
      darkTheme(g_nameEdit, L"DarkMode_CFD");
      positionEdit();

      loadAll();
      refreshPresets();
      setStatus(L"Not connected.");
      {
        std::vector<std::wstring> ports = listPorts();
        if (!g_port.empty() && std::find(ports.begin(), ports.end(), g_port) != ports.end()) connectPort(g_port);
      }
      return 0;
    }

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      paintAll(hdc, rc.right, rc.bottom);
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)wp;
      SetTextColor(dc, RGB(250, 249, 245));
      SetBkColor(dc, RGB(48, 48, 46));
      return (LRESULT)g_editBrush;
    }

    case WM_COMMAND:
      if (LOWORD(wp) == ID_NAME) {
        if (HIWORD(wp) == EN_SETFOCUS) {
          if (g_recIdx >= 0) stopRecording();
          g_editFocus = true;
          refreshGrid();
        } else if (HIWORD(wp) == EN_KILLFOCUS) {
          g_editFocus = false;
          refreshGrid();
        }
      }
      return 0;

    case WM_MOUSEMOVE: {
      int h = hitTest((short)LOWORD(lp), (short)HIWORD(lp));
      if (h != g_hover) { g_hover = h; refreshGrid(); }
      if (!g_tracking) {
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        g_tracking = true;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      g_tracking = false;
      g_hover = -1;
      refreshGrid();
      return 0;

    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT && g_hover >= 0) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
      break;

    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      g_down = hitTest((short)LOWORD(lp), (short)HIWORD(lp));
      if (g_down >= 0) SetCapture(hwnd);
      refreshGrid();
      return 0;

    case WM_LBUTTONUP: {
      int down = g_down;
      g_down = -1;
      if (GetCapture() == hwnd) ReleaseCapture();
      int h = hitTest((short)LOWORD(lp), (short)HIWORD(lp));
      refreshGrid();
      if (down >= 0 && h == down) onWidget(h);
      return 0;
    }

    case WM_RBUTTONUP: {
      int h = hitTest((short)LOWORD(lp), (short)HIWORD(lp));
      if (h >= 0 && g_w[h].kind == W_KEY) {
        if (g_recIdx >= 0) stopRecording();
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        ClientToScreen(hwnd, &pt);
        keyContextMenu(g_w[h].id - ID_KEY0, pt);
      }
      return 0;
    }

    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE && g_recIdx >= 0) {
        stopRecording();
        setStatus(L"Editing cancelled (window lost focus).");
      }
      return 0;

    case WM_TIMER:
      KillTimer(hwnd, 1);
      g_flash = -1;
      refreshGrid();
      return 0;

    case WM_REC_DONE:
      if (g_recIdx >= 0) {
        int idx = g_recIdx;
        g_presets[g_cur].keys[idx] = g_result;
        g_presets[g_cur].cmd[idx].clear();
        stopRecording();
        saveAll();
        setStatus(L"Key " + std::to_wstring(idx + 1) + L" = " + bindingText(g_presets[g_cur].keys[idx]));
      }
      return 0;

    case WM_REC_CANCEL:
      if (g_recIdx >= 0) {
        stopRecording();
        setStatus(L"Editing cancelled.");
      }
      return 0;

    case WM_KEYPRESSED:
      if (g_recIdx < 0) {
        int i = (int)wp - 1;
        const Preset& p = g_presets[g_cur];
        g_flash = i;
        SetTimer(hwnd, 1, 220, nullptr);
        if (!p.cmd[i].empty()) {
          runCommand(p.cmd[i]);
          setStatus(L"Pad key " + std::to_wstring(i + 1) + L"  ->  " + p.cmd[i]);
        } else {
          sendBinding(p.keys[i]);
          setStatus(L"Pad key " + std::to_wstring(i + 1) + L"  ->  " + bindingText(p.keys[i]));
        }
      }
      return 0;

    case WM_LINK_LOST:
      disconnectPort();
      setStatus(L"Connection lost (macropad unplugged?). Press Connect to retry.");
      return 0;

    case WM_DESTROY:
      if (g_hook) UnhookWindowsHookEx(g_hook);
      disconnectPort();
      delete g_fTitle; delete g_fUI; delete g_fSmall; delete g_fNum; delete g_fKey;
      DeleteObject(g_editFont);
      DeleteObject(g_editBrush);
      DeleteObject(g_dlgBrush);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

extern "C" int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int show) {
  SetProcessDPIAware();
  HDC sdc = GetDC(nullptr);
  g_s = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0f;
  ReleaseDC(nullptr, sdc);

  GdiplusStartupInput gin;
  ULONG_PTR gtok = 0;
  GdiplusStartup(&gtok, &gin, nullptr);
  enableDarkMenus();

  WNDCLASSW wc = {};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = L"Macropad config";
  RegisterClassW(&wc);

  WNDCLASSW dc = {};
  dc.lpfnWndProc = dlgProc;
  dc.hInstance = hi;
  dc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  dc.hbrBackground = CreateSolidBrush(RGB(38, 38, 36));
  dc.lpszClassName = L"MacropadCmd";
  RegisterClassW(&dc);

  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
  RECT r = {0, 0, S(CLIENT_W), S(CLIENT_H)};
  AdjustWindowRect(&r, style, FALSE);
  HWND h = CreateWindowW(L"Macropad config", L"Macropad Config", style, CW_USEDEFAULT, CW_USEDEFAULT,
                         r.right - r.left, r.bottom - r.top, nullptr, nullptr, hi, nullptr);
  ShowWindow(h, show);
  UpdateWindow(h);

  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  GdiplusShutdown(gtok);
  return 0;
}