#include "padengine.h"

PadEngine::PadEngine() {
  for (int i = 0; i < kKeyCount; i++) {
    K& s = k_[i];
    s.hold.setSingleShot(true);
    s.dbl.setSingleShot(true);
    QObject::connect(&s.hold, &QTimer::timeout, [this, i] {
      K& s = k_[i];
      if (!s.down) return;
      s.holdFired = true;
      fire(i, Hold);
    });
    QObject::connect(&s.dbl, &QTimer::timeout, [this, i] {
      K& s = k_[i];
      if (!s.pendingTap) return;
      s.pendingTap = false;
      fire(i, Tap);
    });
  }
}

void PadEngine::reset() {
  for (K& s : k_) {
    s.hold.stop();
    s.dbl.stop();
    s.down = s.holdFired = s.pendingTap = s.swallowRelease = false;
  }
  layerDown_ = layerUsed_ = false;
}

void PadEngine::press(int key) {
  if (key < 0 || key >= kKeyCount) return;

  // preset key
  if (key == presetKey) {
    if (!releasesSeen_) {  // old firmware: can't hold, so a press just cycles
      pickPreset(-1);
      return;
    }
    layerDown_ = true;
    layerUsed_ = false;
    return;
  }
  K& s = k_[key];
  if (layerDown_) {
    layerUsed_ = true;
    s.down = true;
    s.swallowRelease = true;
    pickPreset(presetKey >= 0 && key > presetKey ? key - 1 : key);
    return;
  }

  // second press of a double-tap
  if (s.pendingTap) {
    s.dbl.stop();
    s.pendingTap = false;
    s.down = true;
    s.swallowRelease = true;
    fire(key, DoubleTap);
    return;
  }

  const bool hold = releasesSeen_ && hasAction(key, Hold);
  const bool dbl = hasAction(key, DoubleTap);
  s.down = true;
  s.holdFired = false;
  s.swallowRelease = false;

  if (!hold && !dbl) {
    s.swallowRelease = true;
    fire(key, Tap);
    return;
  }
  if (hold) s.hold.start(holdMs);
  if (!releasesSeen_) {  // no release will come: treat the press as the end of a tap
    s.down = false;
    s.pendingTap = true;
    s.dbl.start(doubleMs);
  }
}

void PadEngine::release(int key) {
  if (key < 0 || key >= kKeyCount) return;
  releasesSeen_ = true;

  if (key == presetKey) {
    if (layerDown_ && !layerUsed_) pickPreset(-1);
    layerDown_ = false;
    return;
  }
  K& s = k_[key];
  if (!s.down) return;
  s.down = false;
  s.hold.stop();
  if (s.swallowRelease) {
    s.swallowRelease = false;
    return;
  }
  if (s.holdFired) return;
  if (hasAction(key, DoubleTap)) {
    s.pendingTap = true;
    s.dbl.start(doubleMs);
  } else {
    fire(key, Tap);
  }
}
