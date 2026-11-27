#pragma once

#include <QWidget>
#include <vector>

#include "actions.h"
#include "config.h"
#include "padengine.h"
#include "platform.h"
#include "serial.h"

class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class QTimer;
class KeyTile;
class ShortcutRecorder;

class MainWindow : public QWidget {
 public:
  MainWindow();
  ~MainWindow() override;

 protected:
  void closeEvent(QCloseEvent* e) override;

 private:
  enum StatusKind { Info, Ok, Warn, Error };

  QWidget* buildSidebar();
  QWidget* buildMain();
  QWidget* buildEditor();
  QWidget* page(QLayout* content, QLabel* hint);
  void addPage(QWidget* w);
  void showPage(int index);

  // device
  void refreshPorts();
  QString selectedPort() const;
  bool connectTo(const QString& port, bool quiet);
  void toggleConnect();
  void reconnectTick();
  void updateConnectionUi();
  void onPadEvent(int n, bool down);

  // presets
  void refreshPresetList();
  void switchPreset(int index);
  void newPreset();
  void duplicatePreset();
  void deletePreset();

  // keys
  void refreshMain();
  void refreshTiles();
  void refreshEditor();
  void selectKey(int i);
  void setActionType(int type);
  void actionEdited();  // after editing the selected action's fields
  void showKeyMenu(int i, const QPoint& globalPos);
  void showIconMenu();
  void browseCommand();
  void browseTarget(bool folder);
  void insertScriptLine(const QString& line);
  void validateScript();
  void runAction(int key, int trigger, bool fromPad);
  void testSelected();
  void openSettings();

  KeySlot& slot(int i) { return cfg_.current().keys[i]; }
  Action& action(int i, int trigger) { return slot(i).act[trigger]; }
  Action& cur() { return action(selected_, trigger_); }
  int presetKeyIndex() const { return cfg_.settings.presetKey - 1; }
  void scheduleSave();
  void setStatus(const QString& text, StatusKind kind = Info);

  Config cfg_;
  SerialLink link_;
  PadEngine pad_;
  Executor exec_;
  std::vector<plat::PortInfo> ports_;
  bool injectOk_ = false;
  int selected_ = 0;
  int trigger_ = Tap;
  int recordingKey_ = -1;
  bool userDisconnected_ = false;
  QString lastAutoError_;
  QString lastStatus_;
  StatusKind lastStatusKind_ = Info;

  // sidebar
  QLabel* dot_ = nullptr;
  QLabel* connLabel_ = nullptr;
  QComboBox* portCombo_ = nullptr;
  QPushButton* connectBtn_ = nullptr;
  QListWidget* presetList_ = nullptr;
  QPushButton* deleteBtn_ = nullptr;
  QLabel* warning_ = nullptr;

  // main
  QLabel* title_ = nullptr;
  KeyTile* tiles_[kKeyCount] = {};
  QLabel* status_ = nullptr;

  // editor
  QLabel* editorTitle_ = nullptr;
  QPushButton* iconBtn_ = nullptr;
  QLineEdit* labelEdit_ = nullptr;
  QButtonGroup* triggerGroup_ = nullptr;
  QLabel* triggerHint_ = nullptr;
  QWidget* editorBody_ = nullptr;
  QLabel* presetKeyNote_ = nullptr;
  QButtonGroup* typeGroup_ = nullptr;
  QVBoxLayout* pagesLayout_ = nullptr;
  std::vector<QWidget*> pages_;  // indexed by Action::Type
  ShortcutRecorder* recorder_ = nullptr;
  QPlainTextEdit* textEdit_ = nullptr;
  QPlainTextEdit* scriptEdit_ = nullptr;
  QLabel* scriptMsg_ = nullptr;
  QLineEdit* cmdEdit_ = nullptr;
  QLineEdit* targetEdit_ = nullptr;

  QTimer* saveTimer_ = nullptr;
  QTimer* reconnectTimer_ = nullptr;
};
