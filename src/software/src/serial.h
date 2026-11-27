// Reads "1".."9" lines from the macropad on a background thread and delivers
// them on the GUI thread.
#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

class SerialLink {
 public:
  ~SerialLink();

  std::function<void(int, bool)> onKey;        // (1..9, pressed?) on the GUI thread
  std::function<void()> onLost;                // device vanished, GUI thread

  bool open(const QString& path, QString* err);
  void close();
  bool isOpen() const { return h_ != -1; }
  QString path() const { return path_; }

 private:
  void loop(intptr_t h, quint64 gen);

  QObject ctx_;  // queued callbacks die with this object
  intptr_t h_ = -1;
  std::thread thread_;
  std::atomic<bool> run_{false};
  QString path_;
  quint64 gen_ = 0;
};
