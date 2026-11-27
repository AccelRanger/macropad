#include "actions.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include "keys.h"
#include "platform.h"

static QString unescape(QString s) {
  s.replace(QLatin1String("\\n"), QLatin1String("\n"));
  s.replace(QLatin1String("\\t"), QLatin1String("\t"));
  return s;
}

bool parseScript(const QString& script, std::vector<Step>* out, QString* err) {
  out->clear();
  const QStringList lines = script.split('\n');
  for (int n = 0; n < lines.size(); n++) {
    const QString line = lines[n].trimmed();
    if (line.isEmpty() || line.startsWith('#')) continue;
    const int sp = line.indexOf(' ');
    const QString cmd = (sp < 0 ? line : line.left(sp)).toLower();
    const QString rest = sp < 0 ? QString() : line.mid(sp + 1).trimmed();
    auto fail = [&](const QString& why) {
      if (err) *err = QStringLiteral("Line %1: %2").arg(n + 1).arg(why);
      return false;
    };
    Step s;
    if (cmd == "keys" || cmd == "key" || cmd == "press") {
      s.kind = Step::Keys;
      for (const QString& part : rest.split('+', Qt::SkipEmptyParts)) {
        const KeyDef* k = keyByName(part);
        if (!k) return fail(QStringLiteral("unknown key “%1”").arg(part.trimmed()));
        s.keys << QString::fromLatin1(k->id);
      }
      if (s.keys.isEmpty()) return fail(QStringLiteral("“keys” needs a shortcut, e.g. keys Ctrl+C"));
    } else if (cmd == "text" || cmd == "type") {
      s.kind = Step::Text;
      // keep the text verbatim (including trailing spaces): drop only the leading "text "
      const QString raw = lines[n].mid(lines[n].indexOf(line.left(1)));
      const int rawSp = raw.indexOf(' ');
      s.arg = unescape(rawSp < 0 ? QString() : raw.mid(rawSp + 1));
      if (s.arg.isEmpty()) return fail(QStringLiteral("“text” needs something to type"));
    } else if (cmd == "wait" || cmd == "sleep" || cmd == "delay") {
      s.kind = Step::Wait;
      QString v = rest.toLower();
      double mult = 1;
      if (v.endsWith("ms")) v.chop(2);
      else if (v.endsWith('s')) { v.chop(1); mult = 1000; }
      bool ok = false;
      const double d = v.trimmed().toDouble(&ok) * mult;
      if (!ok || d < 0 || d > 60000) return fail(QStringLiteral("“wait” needs a time like 200 or 1.5s (max 60s)"));
      s.ms = (int)d;
    } else if (cmd == "run" || cmd == "cmd") {
      s.kind = Step::Run;
      s.arg = rest;
      if (s.arg.isEmpty()) return fail(QStringLiteral("“run” needs a command"));
    } else if (cmd == "open") {
      s.kind = Step::Open;
      s.arg = rest;
      if (s.arg.isEmpty()) return fail(QStringLiteral("“open” needs a URL, file or folder"));
    } else {
      return fail(QStringLiteral("unknown step “%1” (use keys, text, wait, run or open)").arg(cmd));
    }
    out->push_back(s);
  }
  if (out->empty()) {
    if (err) *err = QStringLiteral("The sequence is empty.");
    return false;
  }
  return true;
}

QString actionTypeName(int type) {
  switch (type) {
    case Action::Keys: return QStringLiteral("Shortcut");
    case Action::Text: return QStringLiteral("Text");
    case Action::Sequence: return QStringLiteral("Sequence");
    case Action::Command: return QStringLiteral("Command");
    case Action::Open: return QStringLiteral("Open");
  }
  return QStringLiteral("Nothing");
}

QString actionSummary(const Action& a) {
  if (!a.isSet()) return QString();
  switch (a.type) {
    case Action::Keys: return comboText(a.keys);
    case Action::Text: {
      QString t = a.text;
      t.replace('\n', QStringLiteral(" ⏎ "));
      return QStringLiteral("“%1”").arg(t);
    }
    case Action::Sequence: {
      std::vector<Step> steps;
      if (!parseScript(a.script, &steps, nullptr)) return QStringLiteral("Sequence (has errors)");
      return steps.size() == 1 ? QStringLiteral("1 step") : QStringLiteral("%1 steps").arg(steps.size());
    }
    case Action::Command: return QStringLiteral("$ ") + a.command;
    case Action::Open: {
      const QUrl u = QUrl::fromUserInput(a.target);
      if (!u.isLocalFile() && !u.host().isEmpty()) return u.host();
      const QString name = QFileInfo(QString(a.target).remove('"')).fileName();
      return name.isEmpty() ? a.target : name;
    }
  }
  return QString();
}

bool openTarget(const QString& target, QString* err) {
  QString t = target.trimmed();
  if (t.size() > 1 && t.startsWith('"') && t.endsWith('"')) t = t.mid(1, t.size() - 2);
  if (t.startsWith(QLatin1String("~/"))) t = QDir::homePath() + t.mid(1);
  const QUrl url = QUrl::fromUserInput(t, QDir::homePath(), QUrl::AssumeLocalFile);
  if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) {
    if (err) *err = QStringLiteral("Not found: %1").arg(url.toLocalFile());
    return false;
  }
  if (!url.isValid() || !QDesktopServices::openUrl(url)) {
    if (err) *err = QStringLiteral("Could not open %1").arg(t);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
bool Executor::runOne(const Step& s, QString* err) {
  switch (s.kind) {
    case Step::Keys: {
      std::vector<const KeyDef*> keys;
      for (const QString& id : s.keys)
        if (const KeyDef* k = keyById(id)) keys.push_back(k);
      return plat::injectKeys(keys, err);
    }
    case Step::Text: return plat::typeText(s.arg, err);
    case Step::Run: return plat::runCommand(s.arg, err);
    case Step::Open: return openTarget(s.arg, err);
    case Step::Wait: return true;
  }
  return true;
}

bool Executor::run(const Action& a, QString* err) {
  switch (a.type) {
    case Action::Keys: {
      Step s{Step::Keys, a.keys, QString(), 0};
      return runOne(s, err);
    }
    case Action::Text: return plat::typeText(a.text, err);
    case Action::Command: return plat::runCommand(a.command, err);
    case Action::Open: return openTarget(a.target, err);
    case Action::Sequence: {
      auto steps = std::make_shared<std::vector<Step>>();
      if (!parseScript(a.script, steps.get(), err)) return false;
      running_++;
      step(steps, 0);
      return true;
    }
  }
  return true;
}

void Executor::step(std::shared_ptr<std::vector<Step>> steps, size_t i) {
  // Run steps until the next wait, then continue from a timer so the UI stays responsive.
  for (; i < steps->size(); i++) {
    const Step& s = (*steps)[i];
    if (s.kind == Step::Wait) {
      QTimer::singleShot(s.ms, &ctx_, [this, steps, i] { step(steps, i + 1); });
      return;
    }
    QString err;
    if (!runOne(s, &err)) {
      running_--;
      if (onError) onError(QStringLiteral("Sequence stopped at step %1: %2").arg(i + 1).arg(err));
      return;
    }
  }
  running_--;
}
