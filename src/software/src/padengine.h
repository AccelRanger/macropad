// Turns raw pad press/release events into tap / hold / double-tap triggers,
// and handles the optional "preset key" (hold it + another key = pick preset).
//
// Latency rules:
//  - a key with only a tap action fires on press, immediately;
//  - a key with a hold action decides on release or after holdMs;
//  - a key with a double-tap action waits doubleMs after the first tap.
// Old firmware that never sends releases still gets taps and double-taps.
#pragma once

#include <QTimer>
#include <functional>

#include "config.h"

class PadEngine {
 public:
  PadEngine();

  std::function<bool(int key, int trigger)> hasAction;  // key 0..8
  std::function<void(int key, int trigger)> fire;
  std::function<void(int presetIndex)> pickPreset;      // -1 = next preset

  int holdMs = 400;
  int doubleMs = 250;
  int presetKey = -1;  // 0..8, or -1 = off

  void press(int key);
  void release(int key);
  void reset();
  bool releasesSeen() const { return releasesSeen_; }

 private:
  struct K {
    bool down = false;
    bool holdFired = false;
    bool pendingTap = false;      // waiting to see if a second tap follows
    bool swallowRelease = false;  // the press already did its job
    QTimer hold, dbl;
  };
  K k_[kKeyCount];
  bool layerDown_ = false, layerUsed_ = false;
  bool releasesSeen_ = false;
};
