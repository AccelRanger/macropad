// Windows backend: Win32 serial, SendInput, cmd.exe. (Ported from the original main.cpp.)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>

#include <QWidget>
#include <algorithm>
#include <string>

#include "keys.h"
#include "platform.h"

namespace plat {

static int comNumber(const QString& s) { return s.mid(3).toInt(); }

std::vector<PortInfo> listPorts() {
  std::vector<PortInfo> out;

  // GUID_DEVCLASS_PORTS {4D36E978-E325-11CE-BFC1-08002BE10318}
  static const GUID kPorts = {0x4d36e978, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
  HDEVINFO di = SetupDiGetClassDevsW(&kPorts, nullptr, nullptr, DIGCF_PRESENT);
  if (di != INVALID_HANDLE_VALUE) {
    SP_DEVINFO_DATA dd;
    dd.cbSize = sizeof dd;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
      wchar_t port[64] = {0};
      HKEY k = SetupDiOpenDevRegKey(di, &dd, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
      if (k && k != (HKEY)INVALID_HANDLE_VALUE) {
        DWORD sz = sizeof(port) - sizeof(wchar_t), type = 0;
        RegQueryValueExW(k, L"PortName", nullptr, &type, (LPBYTE)port, &sz);
        RegCloseKey(k);
      }
      const QString portName = QString::fromWCharArray(port);
      if (!portName.startsWith("COM")) continue;

      wchar_t name[256] = {0};
      SetupDiGetDeviceRegistryPropertyW(di, &dd, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)name,
                                        sizeof(name) - sizeof(wchar_t), nullptr);
      wchar_t hw[512] = {0};
      SetupDiGetDeviceRegistryPropertyW(di, &dd, SPDRP_HARDWAREID, nullptr, (PBYTE)hw,
                                        sizeof(hw) - 2 * sizeof(wchar_t), nullptr);
      PortInfo pi;
      pi.path = portName;
      QString fn = QString::fromWCharArray(name);
      fn.remove(" (" + portName + ")");
      pi.description = fn.trimmed();
      pi.likelyMacropad = QString::fromWCharArray(hw).contains("VID_303A", Qt::CaseInsensitive);
      out.push_back(pi);
    }
    SetupDiDestroyDeviceInfoList(di);
  }

  if (out.empty()) {  // fallback: plain registry list
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &k) == ERROR_SUCCESS) {
      for (DWORD i = 0;; i++) {
        wchar_t name[256];
        DWORD nl = 256;
        wchar_t data[256] = {0};
        DWORD dl = sizeof(data) - 2, type = 0;
        if (RegEnumValueW(k, i, name, &nl, nullptr, &type, (LPBYTE)data, &dl) != ERROR_SUCCESS) break;
        if (type == REG_SZ) {
          PortInfo pi;
          pi.path = QString::fromWCharArray(data);
          out.push_back(pi);
        }
      }
      RegCloseKey(k);
    }
  }

  std::sort(out.begin(), out.end(),
            [](const PortInfo& a, const PortInfo& b) { return comNumber(a.path) < comNumber(b.path); });
  return out;
}

intptr_t portOpen(const QString& path, QString* err) {
  const std::wstring dev = L"\\\\.\\" + path.toStdWString();
  HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    if (err) {
      if (GetLastError() == ERROR_ACCESS_DENIED)
        *err = QStringLiteral("%1 is busy (is another program using it?).").arg(path);
      else
        *err = QStringLiteral("Could not open %1.").arg(path);
    }
    return -1;
  }
  DCB dcb = {};
  dcb.DCBlength = sizeof dcb;
  GetCommState(h, &dcb);
  dcb.BaudRate = 115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;  // needed for ESP32 USB-CDC to transmit
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fOutX = dcb.fInX = FALSE;
  SetCommState(h, &dcb);
  COMMTIMEOUTS to = {50, 0, 100, 0, 0};  // reads return after <= 100 ms
  SetCommTimeouts(h, &to);
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
  return (intptr_t)h;
}

int portRead(intptr_t h, char* buf, int n) {
  DWORD got = 0;
  if (!ReadFile((HANDLE)h, buf, (DWORD)n, &got, nullptr)) return -1;
  return (int)got;
}

void portClose(intptr_t h) {
  if (h != -1) CloseHandle((HANDLE)h);
}

bool injectInit(QString*) { return true; }
void injectShutdown() {}

bool injectKeys(const std::vector<const KeyDef*>& keys, QString* err) {
  std::vector<INPUT> in;
  auto add = [&](const KeyDef* k, bool up) {
    INPUT i = {};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = (WORD)k->vk;
    i.ki.wScan = (WORD)MapVirtualKeyW(k->vk, MAPVK_VK_TO_VSC);
    i.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (k->vkExt ? KEYEVENTF_EXTENDEDKEY : 0);
    in.push_back(i);
  };
  for (const KeyDef* k : keys) add(k, false);
  for (auto it = keys.rbegin(); it != keys.rend(); ++it) add(*it, true);
  if (SendInput((UINT)in.size(), in.data(), sizeof(INPUT)) != in.size()) {
    if (err) *err = QStringLiteral("Windows blocked the keystrokes (is an elevated window focused?).");
    return false;
  }
  return true;
}

bool typeText(const QString& text, QString* err) {
  std::vector<INPUT> in;
  for (QChar ch : text) {
    INPUT d = {}, u = {};
    d.type = u.type = INPUT_KEYBOARD;
    if (ch == '\n' || ch == '\t') {
      d.ki.wVk = u.ki.wVk = (ch == '\n') ? VK_RETURN : VK_TAB;
      u.ki.dwFlags = KEYEVENTF_KEYUP;
    } else if (ch == '\r') {
      continue;
    } else {
      d.ki.wScan = u.ki.wScan = ch.unicode();  // UTF-16 unit; surrogate pairs go through as two units
      d.ki.dwFlags = KEYEVENTF_UNICODE;
      u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    }
    in.push_back(d);
    in.push_back(u);
  }
  if (in.empty()) return true;
  if (SendInput((UINT)in.size(), in.data(), sizeof(INPUT)) != in.size()) {
    if (err) *err = QStringLiteral("Windows blocked the typing (is an elevated window focused?).");
    return false;
  }
  return true;
}

QString typingNote() { return QStringLiteral("Any characters work, including emoji and accents."); }

bool runCommand(const QString& cmd, QString* err) {
  std::wstring line = L"cmd.exe /s /c \"" + cmd.toStdWString() + L"\"";
  std::vector<wchar_t> buf(line.begin(), line.end());
  buf.push_back(0);
  STARTUPINFOW si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    if (err) *err = QStringLiteral("Could not run the command.");
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

QString shellName() { return QStringLiteral("cmd.exe"); }

QStringList commandExamples() {
  return {QStringLiteral("notepad"), QStringLiteral("start https://github.com"),
          QStringLiteral("\"C:\\Program Files\\App\\app.exe\" --flag")};
}

void decorateWindow(QWidget* w) {
  HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (!dwm) return;
  typedef HRESULT(WINAPI * SetAttr)(HWND, DWORD, LPCVOID, DWORD);
  SetAttr f = (SetAttr)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
  BOOL on = TRUE;
  if (f) f((HWND)w->winId(), 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &on, sizeof on);
}

}  // namespace plat
