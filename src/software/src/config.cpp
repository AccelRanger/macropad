#include "config.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "keys.h"

bool Action::isSet() const {
  switch (type) {
    case Keys: return !keys.isEmpty();
    case Text: return !text.isEmpty();
    case Sequence: return !script.trimmed().isEmpty();
    case Command: return !command.isEmpty();
    case Open: return !target.isEmpty();
  }
  return false;
}

QString Config::dir() {
  return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/MacropadStudio");
}
QString Config::path() { return dir() + QStringLiteral("/config.json"); }

QString Config::resolveIcon(const QString& icon) {
  return icon.startsWith(QLatin1String("file:")) ? dir() + '/' + icon.mid(5) : QString();
}

static const char* kTypeNames[Action::TypeCount] = {"none", "shortcut", "text", "sequence", "command", "open"};
static const char* kTriggerNames[TriggerCount] = {"tap", "hold", "double"};

static QJsonObject actionToJson(const Action& a) {
  QJsonObject o;
  o["type"] = kTypeNames[a.type];
  if (!a.keys.isEmpty()) o["keys"] = QJsonArray::fromStringList(a.keys);
  if (!a.text.isEmpty()) o["text"] = a.text;
  if (!a.script.isEmpty()) o["script"] = a.script;
  if (!a.command.isEmpty()) o["command"] = a.command;
  if (!a.target.isEmpty()) o["target"] = a.target;
  return o;
}

static Action actionFromJson(const QJsonObject& o) {
  Action a;
  for (const QJsonValue& k : o.value("keys").toArray())
    if (const KeyDef* d = keyById(k.toString())) a.keys << d->id;
  a.text = o.value("text").toString();
  a.script = o.value("script").toString();
  a.command = o.value("command").toString();
  a.target = o.value("target").toString();
  const QString t = o.value("type").toString();
  for (int i = 0; i < Action::TypeCount; i++)
    if (t == QLatin1String(kTypeNames[i])) a.type = i;
  return a;
}

void Config::load() {
  presets.clear();
  active = 0;

  QFile f(path());
  if (f.open(QIODevice::ReadOnly)) {
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    port = root.value("port").toString();

    const QJsonObject s = root.value("settings").toObject();
    const QString th = s.value("theme").toString(settings.theme);
    if (th == "dark" || th == "light" || th == "system") settings.theme = th;
    settings.holdMs = qBound(150, s.value("holdMs").toInt(settings.holdMs), 3000);
    settings.doubleMs = qBound(100, s.value("doubleMs").toInt(settings.doubleMs), 1000);
    settings.presetKey = qBound(0, s.value("presetKey").toInt(0), kKeyCount);
    settings.autoReconnect = s.value("autoReconnect").toBool(true);

    const QString activeName = root.value("active").toString();
    for (const QJsonValue& pv : root.value("presets").toArray()) {
      const QJsonObject po = pv.toObject();
      Preset p;
      p.name = cleanName(po.value("name").toString());
      if (p.name.isEmpty() || nameTaken(p.name)) p.name = uniqueName(QStringLiteral("Preset"));
      const QJsonArray keys = po.value("keys").toArray();
      for (int i = 0; i < kKeyCount && i < keys.size(); i++) {
        const QJsonObject ko = keys[i].toObject();
        KeySlot& slot = p.keys[i];
        if (!ko.contains("tap") && ko.contains("type")) {  // v2 file: the key *is* the tap action
          slot.act[Tap] = actionFromJson(ko);
          continue;
        }
        slot.label = ko.value("label").toString();
        slot.icon = ko.value("icon").toString();
        for (int t = 0; t < TriggerCount; t++) slot.act[t] = actionFromJson(ko.value(kTriggerNames[t]).toObject());
      }
      presets.push_back(p);
      if (p.name == activeName) active = (int)presets.size() - 1;
    }
  } else if (importLegacy()) {
    save();
  }

  if (presets.empty()) {
    Preset p;
    p.name = QStringLiteral("Default");
    presets.push_back(p);
    active = 0;
  }
}

bool Config::save() const {
  QJsonArray ps;
  for (const Preset& p : presets) {
    QJsonArray keys;
    for (const KeySlot& slot : p.keys) {
      QJsonObject ko;
      if (!slot.label.isEmpty()) ko["label"] = slot.label;
      if (!slot.icon.isEmpty()) ko["icon"] = slot.icon;
      for (int t = 0; t < TriggerCount; t++) {
        const Action& a = slot.act[t];
        if (t == Tap || a.type != Action::None || a.isSet() || !a.keys.isEmpty())
          ko[kTriggerNames[t]] = actionToJson(a);
      }
      keys.append(ko);
    }
    QJsonObject po;
    po["name"] = p.name;
    po["keys"] = keys;
    ps.append(po);
  }
  QJsonObject s;
  s["theme"] = settings.theme;
  s["holdMs"] = settings.holdMs;
  s["doubleMs"] = settings.doubleMs;
  s["presetKey"] = settings.presetKey;
  s["autoReconnect"] = settings.autoReconnect;

  QJsonObject root;
  root["version"] = 3;
  root["port"] = port;
  root["active"] = presets.empty() ? QString() : presets[active].name;
  root["settings"] = s;
  root["presets"] = ps;

  QDir().mkpath(dir());
  QSaveFile f(path());
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  return f.commit();
}

// Old Windows-only format: INI-ish text file holding Windows virtual-key codes
// (with 0x100 set for extended keys).
bool Config::importLegacy() {
  const QString appdata = qEnvironmentVariable("APPDATA");
  if (appdata.isEmpty()) return false;
  QFile f(appdata + QStringLiteral("/MacropadStudio/presets.txt"));
  if (!f.open(QIODevice::ReadOnly)) return false;

  QString activeName;
  Preset* cur = nullptr;
  const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty()) continue;
    if (line.startsWith('[') && line.endsWith(']')) {
      presets.push_back(Preset());
      cur = &presets.back();
      cur->name = cleanName(line.mid(1, line.size() - 2));
    } else if (line.startsWith("port=")) {
      port = line.mid(5);
    } else if (line.startsWith("active=")) {
      activeName = line.mid(7);
    } else if (cur && line.startsWith("cmd")) {
      const int eq = line.indexOf('=');
      const int i = line.mid(3, eq - 3).toInt() - 1;
      if (eq > 0 && i >= 0 && i < kKeyCount) {
        Action& a = cur->keys[i].act[Tap];
        a.command = line.mid(eq + 1);
        if (!a.command.isEmpty()) a.type = Action::Command;
      }
    } else if (cur && line[0].isDigit()) {
      const int eq = line.indexOf('=');
      const int i = line.left(eq).toInt() - 1;
      if (eq < 0 || i < 0 || i >= kKeyCount) continue;
      Action& a = cur->keys[i].act[Tap];
      for (const QString& v : line.mid(eq + 1).split(',', Qt::SkipEmptyParts)) {
        const int code = v.toInt();
        const KeyDef* k = keyByVk(code & 0xFF, (code & 0x100) ? 0x100 : 0);
        if (k && !a.keys.contains(k->id)) a.keys << k->id;
      }
      if (!a.keys.isEmpty() && a.type == Action::None) a.type = Action::Keys;
    }
  }
  for (size_t i = 0; i < presets.size(); i++)
    if (presets[i].name == activeName) active = (int)i;
  return !presets.empty();
}

bool Config::nameTaken(const QString& name, int ignore) const {
  for (size_t i = 0; i < presets.size(); i++)
    if ((int)i != ignore && presets[i].name.compare(name, Qt::CaseInsensitive) == 0) return true;
  return false;
}

QString Config::uniqueName(const QString& base) const {
  if (!nameTaken(base)) return base;
  for (int i = 2;; i++) {
    const QString n = base + ' ' + QString::number(i);
    if (!nameTaken(n)) return n;
  }
}

QString Config::cleanName(QString s) {
  s.remove('\r').remove('\n');
  return s.simplified().left(40);
}
