// Running actions, including multi-step sequences.
//
// Sequence script: one step per line, '#' starts a comment.
//   keys Ctrl+Shift+P     press a shortcut (key names as shown in the app)
//   text Hello world      type text ("\n" = Enter, "\t" = Tab)
//   wait 150              pause, in ms (also "1.5s")
//   run notepad           run a command
//   open https://…        open a URL, file or folder
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

#include "config.h"

struct Step {
  enum Kind { Keys, Text, Wait, Run, Open } kind;
  QStringList keys;
  QString arg;
  int ms = 0;
};

bool parseScript(const QString& script, std::vector<Step>* out, QString* err);

QString actionTypeName(int type);
QString actionSummary(const Action& a);  // one short line, e.g. "Ctrl + C", "“Hello”", "4 steps"

bool openTarget(const QString& target, QString* err);

class Executor {
 public:
  std::function<void(const QString&)> onError;  // async failures inside sequences

  // Starts the action. Returns false (and fills err) if it failed right away.
  bool run(const Action& a, QString* err);
  bool busy() const { return running_ > 0; }

 private:
  void step(std::shared_ptr<std::vector<Step>> steps, size_t i);
  bool runOne(const Step& s, QString* err);

  QObject ctx_;
  int running_ = 0;
};
