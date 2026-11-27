#include "serial.h"

#include <QMetaObject>
#include <cstdlib>
#include <string>

#include "config.h"
#include "platform.h"

SerialLink::~SerialLink() { close(); }

bool SerialLink::open(const QString& path, QString* err) {
  close();
  const intptr_t h = plat::portOpen(path, err);
  if (h == -1) return false;
  h_ = h;
  path_ = path;
  run_ = true;
  thread_ = std::thread(&SerialLink::loop, this, h, ++gen_);
  return true;
}

void SerialLink::close() {
  run_ = false;
  if (thread_.joinable()) thread_.join();
  if (h_ != -1) plat::portClose(h_);
  h_ = -1;
  ++gen_;  // drop callbacks that are still queued
}

void SerialLink::loop(intptr_t h, quint64 gen) {
  std::string line;
  char buf[64];
  while (run_) {
    const int n = plat::portRead(h, buf, sizeof buf);
    if (n < 0) {
      if (run_)
        QMetaObject::invokeMethod(&ctx_, [this, gen] {
          if (gen != gen_) return;
          close();
          if (onLost) onLost();
        }, Qt::QueuedConnection);
      return;
    }
    for (int i = 0; i < n; i++) {
      const char c = buf[i];
      if (c == '\n') {
        // "N" = key N pressed, "rN" = key N released
        const bool down = line.empty() || (line[0] != 'r' && line[0] != 'R');
        const int v = atoi(line.c_str() + (down ? 0 : 1));
        if (v >= 1 && v <= kKeyCount)
          QMetaObject::invokeMethod(&ctx_, [this, gen, v, down] {
            if (gen == gen_ && onKey) onKey(v, down);
          }, Qt::QueuedConnection);
        line.clear();
      } else if (c != '\r' && line.size() < 16) {
        line += c;
      }
    }
  }
}
