// Presets + settings, stored as JSON:
//   Linux:   ~/.config/MacropadStudio/config.json
//   Windows: %LOCALAPPDATA%\MacropadStudio\config.json
// On first run on Windows, the old %APPDATA%\MacropadStudio\presets.txt is imported.
#pragma once

#include <QString>
#include <QStringList>
#include <vector>

static const int kKeyCount = 9;  // 3x3 pad

struct Action {
  // Values are stored by name in the JSON, so the numbers can change freely.
  enum Type { None = 0, Keys, Text, Sequence, Command, Open, TypeCount };
  int type = None;
  QStringList keys;  // Keys: key ids, see keys.h
  QString text;      // Text: typed verbatim
  QString script;    // Sequence: one step per line, see actions.h
  QString command;   // Command: shell command line
  QString target;    // Open: file, folder or URL

  bool isSet() const;
};

enum Trigger { Tap = 0, Hold = 1, DoubleTap = 2, TriggerCount };

struct KeySlot {
  QString label;  // optional name shown on the tile
  QString icon;   // "" | emoji/short text | "file:icons/<name>" (relative to the config dir)
  Action act[TriggerCount];
};

struct Preset {
  QString name;
  KeySlot keys[kKeyCount];
};

struct Settings {
  QString theme = QStringLiteral("system");  // system | dark | light
  int holdMs = 400;
  int doubleMs = 250;
  int presetKey = 0;  // 0 = off, 1..9 = that pad key switches presets
  bool autoReconnect = true;
};

class Config {
 public:
  QString port;
  int active = 0;
  std::vector<Preset> presets;
  Settings settings;

  void load();
  bool save() const;
  static QString dir();
  static QString path();
  static QString resolveIcon(const QString& icon);  // file path for "file:" icons, else empty

  Preset& current() { return presets[active]; }
  bool nameTaken(const QString& name, int ignore = -1) const;
  QString uniqueName(const QString& base) const;
  static QString cleanName(QString s);

 private:
  bool importLegacy();
};
