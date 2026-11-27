// Platform-neutral key table. Bindings are stored by `id` (e.g. "Ctrl", "C",
// "VolumeUp"), and each platform maps that to its own native code.
#pragma once

#include <QString>
#include <QStringList>
#include <vector>

class QKeyEvent;

struct KeyDef {
  const char* id;     // stable id written to the config file
  const char* label;  // text shown in the UI
  int vk;             // Windows virtual-key code (0 = none)
  bool vkExt;         // Windows: needs KEYEVENTF_EXTENDEDKEY
  int evdev;          // Linux input-event code (0 = none)
  int qt;             // Qt::Key fallback used when no native code is available
  bool modifier;
};

const std::vector<KeyDef>& allKeys();
const KeyDef* keyById(const QString& id);
const KeyDef* keyByEvdev(int code);
const KeyDef* keyByVk(int vk, int scanCode);  // scanCode may carry the 0x100 "extended" bit
const KeyDef* keyByQt(int qtKey);
const KeyDef* keyFromEvent(const QKeyEvent* e);
const KeyDef* keyByName(const QString& name);  // id, label or common alias ("Control", "Win", "Return"…)

QString keyLabel(const QString& id);
QString comboText(const QStringList& ids);           // "Ctrl + Shift + C"
QStringList sortCombo(const QStringList& ids);       // modifiers first, in a fixed order
