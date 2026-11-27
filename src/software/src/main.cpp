// Macropad Studio — desktop companion for the 3x3 ESP32-C6 macropad.
// The pad prints the pressed key number ("1".."9") over USB serial; this app
// maps each number to a keyboard shortcut or a shell command, per preset.
#include <QApplication>

#include "mainwindow.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Macropad Studio"));
  QApplication::setApplicationVersion(QStringLiteral("2.0"));
  QApplication::setDesktopFileName(QStringLiteral("macropad-studio"));  // Wayland app id

  MainWindow w;
  w.show();
  return app.exec();
}
