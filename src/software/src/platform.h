// Everything OS-specific lives behind this interface:
//   platform_linux.cpp  (termios serial, uinput key injection, /bin/sh)
//   platform_win.cpp    (Win32 serial, SendInput, cmd.exe)
#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

struct KeyDef;
class QWidget;

namespace plat {

struct PortInfo {
  QString path;         // "/dev/ttyACM0" or "COM3"
  QString description;  // "Espressif USB JTAG/serial debug unit", may be empty
  bool likelyMacropad = false;  // Espressif USB vendor id (0x303A)
};

std::vector<PortInfo> listPorts();

// Serial: handle is -1 when invalid.
intptr_t portOpen(const QString& path, QString* err);
int portRead(intptr_t h, char* buf, int n);  // >0 bytes, 0 = timeout (~100 ms), <0 = link lost
void portClose(intptr_t h);

// Keystroke injection.
bool injectInit(QString* err);
void injectShutdown();
bool injectKeys(const std::vector<const KeyDef*>& keys, QString* err);

// Types arbitrary text into the focused window.
bool typeText(const QString& text, QString* err);
QString typingNote();  // limitations shown under the Text editor

// Runs a command line through the platform shell, detached.
bool runCommand(const QString& cmd, QString* err);
QString shellName();
QStringList commandExamples();

void decorateWindow(QWidget* w);  // e.g. dark title bar on Windows

}  // namespace plat
