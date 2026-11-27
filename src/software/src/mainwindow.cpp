#include "mainwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextBlock>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include "keys.h"
#include "theme.h"
#include "widgets.h"

static QLabel* label(const QString& text, const char* objectName = nullptr) {
  auto* l = new QLabel(text);
  if (objectName) l->setObjectName(QString::fromLatin1(objectName));
  return l;
}

static QPushButton* button(const QString& text, const char* objectName = nullptr) {
  auto* b = new QPushButton(text);
  if (objectName) b->setObjectName(QString::fromLatin1(objectName));
  b->setCursor(Qt::PointingHandCursor);
  return b;
}

static QString triggerName(int t) {
  return t == Hold ? QStringLiteral("hold") : t == DoubleTap ? QStringLiteral("double-tap") : QStringLiteral("tap");
}

static QApplication& app() { return *static_cast<QApplication*>(QCoreApplication::instance()); }

MainWindow::MainWindow() {
  setWindowTitle(QStringLiteral("Macropad Studio"));
  setWindowIcon(theme::appIcon());
  resize(1080, 820);
  setMinimumSize(940, 720);

  cfg_.load();
  theme::apply(app(), cfg_.settings.theme);

  saveTimer_ = new QTimer(this);
  saveTimer_->setSingleShot(true);
  saveTimer_->setInterval(400);
  connect(saveTimer_, &QTimer::timeout, this, [this] { cfg_.save(); });

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(buildSidebar());
  root->addWidget(buildMain(), 1);

  // pad -> gesture engine -> actions
  pad_.holdMs = cfg_.settings.holdMs;
  pad_.doubleMs = cfg_.settings.doubleMs;
  pad_.presetKey = presetKeyIndex();
  pad_.hasAction = [this](int k, int t) { return action(k, t).isSet(); };
  pad_.fire = [this](int k, int t) { runAction(k, t, true); };
  pad_.pickPreset = [this](int idx) {
    const int n = (int)cfg_.presets.size();
    if (idx < 0) idx = (cfg_.active + 1) % n;
    if (idx >= n) {
      setStatus(QStringLiteral("There is no preset #%1.").arg(idx + 1), Warn);
      return;
    }
    switchPreset(idx);
  };
  exec_.onError = [this](const QString& e) { setStatus(e, Error); };

  link_.onKey = [this](int n, bool down) { onPadEvent(n, down); };
  link_.onLost = [this] {
    pad_.reset();
    updateConnectionUi();
    setStatus(cfg_.settings.autoReconnect
                  ? QStringLiteral("Macropad disconnected. Waiting for it to come back…")
                  : QStringLiteral("Connection lost (macropad unplugged?). Press Connect to retry."),
              Warn);
  };

  QString err;
  injectOk_ = plat::injectInit(&err);
  if (!injectOk_) {
    warning_->setText(err);
    warning_->show();
  }

  theme::setOnChanged([this] {
    updateConnectionUi();
    refreshEditor();
    setStatus(lastStatus_, lastStatusKind_);
  });

  refreshPresetList();
  refreshMain();
  refreshPorts();
  updateConnectionUi();
  setStatus(QStringLiteral("Ready."));

  reconnectTimer_ = new QTimer(this);
  reconnectTimer_->setInterval(1500);
  connect(reconnectTimer_, &QTimer::timeout, this, [this] { reconnectTick(); });
  reconnectTimer_->start();
  // first attempt right away: saved port, or anything that looks like the macropad
  QTimer::singleShot(0, this, [this] {
    const QString p = selectedPort();
    for (const plat::PortInfo& info : ports_)
      if (info.path == p && (p == cfg_.port || info.likelyMacropad)) {
        connectTo(p, false);
        return;
      }
  });
  plat::decorateWindow(this);
}

MainWindow::~MainWindow() {
  theme::setOnChanged(nullptr);
  link_.close();
  plat::injectShutdown();
}

void MainWindow::closeEvent(QCloseEvent* e) {
  saveTimer_->stop();
  cfg_.save();
  QWidget::closeEvent(e);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
QWidget* MainWindow::buildSidebar() {
  auto* side = new QFrame;
  side->setObjectName("sidebar");
  side->setFixedWidth(284);
  auto* v = new QVBoxLayout(side);
  v->setContentsMargins(20, 22, 20, 20);
  v->setSpacing(10);

  // brand + settings
  auto* brandRow = new QHBoxLayout;
  brandRow->setSpacing(8);
  auto* logo = new QLabel;
  logo->setPixmap(theme::appIcon().pixmap(30, 30));
  brandRow->addWidget(logo);
  brandRow->addWidget(label(QStringLiteral("Macropad"), "brand"));
  brandRow->addWidget(label(QStringLiteral("Studio"), "brandSub"));
  brandRow->addStretch();
  auto* gear = button(QStringLiteral("⚙"), "ghost");
  gear->setToolTip(QStringLiteral("Settings"));
  gear->setStyleSheet(QStringLiteral("font-size: 15pt; padding: 2px 8px;"));
  brandRow->addWidget(gear);
  connect(gear, &QPushButton::clicked, this, [this] { openSettings(); });
  v->addLayout(brandRow);
  v->addSpacing(16);

  // device card
  v->addWidget(label(QStringLiteral("DEVICE"), "section"));
  auto* card = new QFrame;
  card->setObjectName("card");
  auto* cv = new QVBoxLayout(card);
  cv->setContentsMargins(14, 12, 14, 14);
  cv->setSpacing(10);

  auto* st = new QHBoxLayout;
  st->setSpacing(8);
  dot_ = new QLabel;
  dot_->setFixedSize(9, 9);
  connLabel_ = new QLabel;
  st->addWidget(dot_);
  st->addWidget(connLabel_, 1);
  cv->addLayout(st);

  auto* portRow = new QHBoxLayout;
  portRow->setSpacing(6);
  portCombo_ = new QComboBox;
  portCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  portCombo_->setMinimumContentsLength(8);
  portCombo_->setCursor(Qt::PointingHandCursor);
  auto* refresh = button(QStringLiteral("↻"));
  refresh->setToolTip(QStringLiteral("Rescan serial ports"));
  refresh->setFixedWidth(38);
  portRow->addWidget(portCombo_, 1);
  portRow->addWidget(refresh);
  cv->addLayout(portRow);

  connectBtn_ = button(QStringLiteral("Connect"), "primary");
  cv->addWidget(connectBtn_);
  v->addWidget(card);
  v->addSpacing(14);

  connect(refresh, &QPushButton::clicked, this, [this] {
    refreshPorts();
    setStatus(ports_.empty() ? QStringLiteral("No serial ports found. Plug in the macropad and rescan.")
                             : QStringLiteral("Found %1 serial port(s).").arg(ports_.size()));
  });
  connect(connectBtn_, &QPushButton::clicked, this, [this] { toggleConnect(); });
  connect(portCombo_, &QComboBox::activated, this, [this](int) {
    const QString p = selectedPort();
    if (p.isEmpty() || p == cfg_.port) return;
    userDisconnected_ = false;
    if (link_.isOpen()) {
      connectTo(p, false);
    } else {
      cfg_.port = p;
      scheduleSave();
    }
  });

  // presets
  v->addWidget(label(QStringLiteral("PRESETS"), "section"));
  presetList_ = new QListWidget;
  presetList_->setObjectName("presets");
  presetList_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
  presetList_->setCursor(Qt::PointingHandCursor);
  presetList_->setToolTip(QStringLiteral("Double-click or F2 to rename"));
  v->addWidget(presetList_, 1);

  auto* pr = new QHBoxLayout;
  pr->setSpacing(6);
  auto* add = button(QStringLiteral("New"), "small");
  auto* dup = button(QStringLiteral("Duplicate"), "small");
  deleteBtn_ = button(QStringLiteral("Delete"), "small");
  pr->addWidget(add, 1);
  pr->addWidget(dup, 1);
  pr->addWidget(deleteBtn_, 1);
  v->addLayout(pr);

  connect(add, &QPushButton::clicked, this, [this] { newPreset(); });
  connect(dup, &QPushButton::clicked, this, [this] { duplicatePreset(); });
  connect(deleteBtn_, &QPushButton::clicked, this, [this] { deletePreset(); });
  connect(presetList_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row < 0 || row >= (int)cfg_.presets.size() || row == cfg_.active) return;
    recorder_->cancel();
    pad_.reset();
    cfg_.active = row;
    scheduleSave();
    refreshMain();
    setStatus(QStringLiteral("Switched to preset “%1”.").arg(cfg_.current().name));
  });
  connect(presetList_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    const int row = presetList_->row(item);
    if (row < 0 || row >= (int)cfg_.presets.size()) return;
    const QString name = Config::cleanName(item->text());
    QString problem;
    if (name.isEmpty()) problem = QStringLiteral("A preset needs a name.");
    else if (cfg_.nameTaken(name, row)) problem = QStringLiteral("A preset called “%1” already exists.").arg(name);
    if (!problem.isEmpty()) {
      QSignalBlocker b(presetList_);
      item->setText(cfg_.presets[row].name);
      setStatus(problem, Warn);
      return;
    }
    if (name == cfg_.presets[row].name) return;
    cfg_.presets[row].name = name;
    {
      QSignalBlocker b(presetList_);
      item->setText(name);
    }
    scheduleSave();
    refreshMain();
    setStatus(QStringLiteral("Renamed preset to “%1”.").arg(name), Ok);
  });

  warning_ = label(QString(), "warning");
  warning_->setWordWrap(true);
  warning_->hide();
  v->addSpacing(6);
  v->addWidget(warning_);
  return side;
}

QWidget* MainWindow::buildMain() {
  auto* main = new QFrame;
  main->setObjectName("main");
  auto* v = new QVBoxLayout(main);
  v->setContentsMargins(34, 26, 34, 18);
  v->setSpacing(0);

  title_ = label(QString(), "title");
  v->addWidget(title_);
  v->addSpacing(2);
  v->addWidget(label(QStringLiteral("Click a key to edit it  ·  double-click to record a shortcut  ·  right-click for quick actions"), "hint"));
  v->addSpacing(18);

  auto* grid = new QGridLayout;
  grid->setSpacing(14);
  for (int i = 0; i < kKeyCount; i++) {
    auto* t = new KeyTile(i);
    t->onClick = [this](int k) { selectKey(k); };
    t->onDoubleClick = [this](int k) {
      selectKey(k);
      if (k == presetKeyIndex()) return;
      setActionType(Action::Keys);
      recorder_->start();
    };
    t->onContextMenu = [this](int k, QPoint pos) { showKeyMenu(k, pos); };
    tiles_[i] = t;
    grid->addWidget(t, i / 3, i % 3);
  }
  for (int i = 0; i < 3; i++) {
    grid->setRowStretch(i, 1);
    grid->setColumnStretch(i, 1);
  }
  v->addLayout(grid, 1);
  v->addSpacing(20);
  v->addWidget(buildEditor());
  v->addSpacing(12);

  status_ = label(QString(), "status");
  status_->setTextFormat(Qt::RichText);
  v->addWidget(status_);
  return main;
}

QWidget* MainWindow::page(QLayout* content, QLabel* hint) {
  auto* w = new QWidget;
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(8);
  if (content) l->addLayout(content);
  if (hint) {
    hint->setWordWrap(true);
    l->addWidget(hint);
  }
  l->addStretch();
  return w;
}

void MainWindow::addPage(QWidget* w) {
  w->hide();
  pages_.push_back(w);
  pagesLayout_->addWidget(w);
}

// Only the current page is visible, so the editor is only as tall as that page.
void MainWindow::showPage(int index) {
  for (int i = 0; i < (int)pages_.size(); i++) pages_[i]->setVisible(i == index);
}

QWidget* MainWindow::buildEditor() {
  auto* ed = new QFrame;
  ed->setObjectName("editor");
  auto* v = new QVBoxLayout(ed);
  v->setContentsMargins(20, 16, 20, 18);
  v->setSpacing(12);

  // row 1: key title, icon, label, trigger tabs
  auto* top = new QHBoxLayout;
  top->setSpacing(10);
  editorTitle_ = label(QString(), "editorTitle");
  editorTitle_->setMinimumWidth(56);
  top->addWidget(editorTitle_);
  iconBtn_ = button(QString(), "iconBtn");
  iconBtn_->setFixedSize(40, 40);
  iconBtn_->setIconSize(QSize(26, 26));
  iconBtn_->setToolTip(QStringLiteral("Icon shown on the key"));
  top->addWidget(iconBtn_);
  labelEdit_ = new QLineEdit;
  labelEdit_->setPlaceholderText(QStringLiteral("Name this key (optional)"));
  labelEdit_->setMaxLength(32);
  top->addWidget(labelEdit_, 1);
  top->addSpacing(8);

  auto segment = [](QButtonGroup* group, const std::vector<std::pair<int, QString>>& items) {
    auto* seg = new QFrame;
    seg->setObjectName("segment");
    auto* sl = new QHBoxLayout(seg);
    sl->setContentsMargins(3, 3, 3, 3);
    sl->setSpacing(2);
    group->setExclusive(true);
    for (const auto& it : items) {
      auto* b = button(it.second, "seg");
      b->setCheckable(true);
      group->addButton(b, it.first);
      sl->addWidget(b);
    }
    return seg;
  };
  triggerGroup_ = new QButtonGroup(this);
  top->addWidget(segment(triggerGroup_, {{Tap, QStringLiteral("Tap")},
                                         {Hold, QStringLiteral("Hold")},
                                         {DoubleTap, QStringLiteral("Double-tap")}}));
  v->addLayout(top);

  triggerHint_ = label(QString(), "hint");
  triggerHint_->setWordWrap(true);
  v->addWidget(triggerHint_);

  presetKeyNote_ = label(QString(), "muted");
  presetKeyNote_->setWordWrap(true);
  v->addWidget(presetKeyNote_);

  // body: action type + its editor
  editorBody_ = new QWidget;
  auto* bv = new QVBoxLayout(editorBody_);
  bv->setContentsMargins(0, 0, 0, 0);
  bv->setSpacing(12);
  auto* typeRow = new QHBoxLayout;
  typeGroup_ = new QButtonGroup(this);
  typeRow->addWidget(segment(typeGroup_, {{Action::Keys, QStringLiteral("Shortcut")},
                                          {Action::Text, QStringLiteral("Type text")},
                                          {Action::Sequence, QStringLiteral("Sequence")},
                                          {Action::Command, QStringLiteral("Command")},
                                          {Action::Open, QStringLiteral("Open")},
                                          {Action::None, QStringLiteral("Nothing")}}));
  typeRow->addStretch();
  auto* test = button(QStringLiteral("▶  Test"), "ghost");
  test->setToolTip(QStringLiteral("Run this action now"));
  typeRow->addWidget(test);
  connect(test, &QPushButton::clicked, this, [this] { testSelected(); });
  bv->addLayout(typeRow);

  auto* pagesBox = new QWidget;
  pagesLayout_ = new QVBoxLayout(pagesBox);
  pagesLayout_->setContentsMargins(0, 0, 0, 0);
  // pages are added in Action::Type order
  // None
  addPage(page(nullptr, label(QStringLiteral("Nothing happens on this trigger. Pick an action type above."), "muted")));

  // Keys
  {
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    recorder_ = new ShortcutRecorder;
    auto* special = button(QStringLiteral("Special keys"));
    auto* menu = new QMenu(special);
    const std::vector<std::pair<QString, QStringList>> groups = {
        {QStringLiteral("Media"), {"PlayPause", "NextTrack", "PrevTrack", "MediaStop"}},
        {QStringLiteral("Volume"), {"VolumeUp", "VolumeDown", "Mute"}},
        {QStringLiteral("F13 – F24"), {"F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"}},
        {QStringLiteral("Other"), {"Esc", "PrintScreen", "Super", "Menu", "CapsLock", "NumLock", "ScrollLock", "Pause"}}};
    for (const auto& g : groups) {
      QMenu* sub = menu->addMenu(g.first);
      for (const QString& id : g.second)
        sub->addAction(keyLabel(id), this, [this, id] {
          recorder_->cancel();
          cur().keys = QStringList{id};
          cur().type = Action::Keys;
          actionEdited();
          setStatus(QStringLiteral("Key %1 (%2) now sends %3.").arg(selected_ + 1).arg(triggerName(trigger_), keyLabel(id)), Ok);
        });
    }
    special->setMenu(menu);
    auto* clear = button(QStringLiteral("Clear"), "ghost");
    row->addWidget(recorder_, 1);
    row->addWidget(special);
    row->addWidget(clear);
    addPage(page(row, label(QStringLiteral("Click the field and press a key or combination. Modifiers alone "
                                                     "(e.g. Ctrl+Shift) are saved when you let go. Esc cancels. Keys your "
                                                     "desktop grabs (media, Super…) are under “Special keys”."),
                                      "hint")));

    connect(clear, &QPushButton::clicked, this, [this] {
      recorder_->cancel();
      cur().keys.clear();
      actionEdited();
    });
    recorder_->onStarted = [this] {
      recordingKey_ = selected_;
      refreshTiles();
      setStatus(QStringLiteral("Recording key %1 (%2): press a key or a shortcut…").arg(selected_ + 1).arg(triggerName(trigger_)));
    };
    recorder_->onCancelled = [this] {
      recordingKey_ = -1;
      refreshTiles();
      setStatus(QStringLiteral("Recording cancelled."));
    };
    recorder_->onRecorded = [this](const QStringList& keys) {
      recordingKey_ = -1;
      cur().keys = keys;
      cur().type = Action::Keys;
      actionEdited();
      refreshTiles();
      setStatus(QStringLiteral("Key %1 (%2) now sends %3.").arg(selected_ + 1).arg(triggerName(trigger_), comboText(keys)), Ok);
    };
    recorder_->onMessage = [this](const QString& m) { setStatus(m, Warn); };
  }

  // Text
  {
    auto* row = new QHBoxLayout;
    textEdit_ = new QPlainTextEdit;
    textEdit_->setPlaceholderText(QStringLiteral("Text to type, e.g. your email signature…"));
    textEdit_->setFixedHeight(84);
    textEdit_->setTabChangesFocus(true);
    row->addWidget(textEdit_);
    addPage(page(row, label(plat::typingNote(), "hint")));
    connect(textEdit_, &QPlainTextEdit::textChanged, this, [this] {
      cur().text = textEdit_->toPlainText();
      cur().type = Action::Text;
      actionEdited();
    });
  }

  // Sequence
  {
    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    scriptEdit_ = new QPlainTextEdit;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(font().pointSizeF());
    scriptEdit_->setFont(mono);
    scriptEdit_->setFixedHeight(124);
    scriptEdit_->setTabChangesFocus(true);
    scriptEdit_->setPlaceholderText(QStringLiteral("keys Ctrl+L\nwait 100\ntext github.com\\n"));
    row->addWidget(scriptEdit_, 1);
    auto* col = new QVBoxLayout;
    col->setSpacing(6);
    auto* addKeys = button(QStringLiteral("+ Shortcut…"), "small");
    auto* addText = button(QStringLiteral("+ Text"), "small");
    auto* addWait = button(QStringLiteral("+ Wait"), "small");
    auto* addRun = button(QStringLiteral("+ Command"), "small");
    for (auto* b : {addKeys, addText, addWait, addRun}) col->addWidget(b);
    col->addStretch();
    row->addLayout(col);
    scriptMsg_ = label(QString(), "hint");
    addPage(page(row, scriptMsg_));

    connect(scriptEdit_, &QPlainTextEdit::textChanged, this, [this] {
      cur().script = scriptEdit_->toPlainText();
      cur().type = Action::Sequence;
      validateScript();
      actionEdited();
    });
    connect(addText, &QPushButton::clicked, this, [this] { insertScriptLine(QStringLiteral("text ")); });
    connect(addWait, &QPushButton::clicked, this, [this] { insertScriptLine(QStringLiteral("wait 100")); });
    connect(addRun, &QPushButton::clicked, this, [this] { insertScriptLine(QStringLiteral("run ")); });
    connect(addKeys, &QPushButton::clicked, this, [this] {
      QDialog d(this);
      d.setWindowTitle(QStringLiteral("Add a shortcut step"));
      auto* l = new QVBoxLayout(&d);
      l->setContentsMargins(20, 18, 20, 18);
      l->addWidget(label(QStringLiteral("Press the shortcut to add. Esc cancels."), "muted"));
      auto* rec = new ShortcutRecorder;
      rec->setMinimumWidth(360);
      l->addWidget(rec);
      QStringList got;
      rec->onRecorded = [&](const QStringList& k) { got = k; d.accept(); };
      rec->onCancelled = [&] { d.reject(); };
      QTimer::singleShot(0, rec, [rec] { rec->start(); });
      if (d.exec() == QDialog::Accepted && !got.isEmpty()) insertScriptLine(QStringLiteral("keys ") + got.join('+'));
    });
  }

  // Command
  {
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    cmdEdit_ = new QLineEdit;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(font().pointSizeF());
    cmdEdit_->setFont(mono);
    cmdEdit_->setPlaceholderText(plat::commandExamples().value(0));
    cmdEdit_->setClearButtonEnabled(true);
    auto* browse = button(QStringLiteral("Browse…"));
    row->addWidget(cmdEdit_, 1);
    row->addWidget(browse);
    addPage(page(row, label(QStringLiteral("Runs through %1, e.g.  %2")
                                          .arg(plat::shellName(), plat::commandExamples().join(QStringLiteral("  ·  "))),
                                      "hint")));
    connect(cmdEdit_, &QLineEdit::textEdited, this, [this](const QString& t) {
      cur().command = t.trimmed();
      cur().type = Action::Command;
      actionEdited();
    });
    connect(browse, &QPushButton::clicked, this, [this] { browseCommand(); });
  }

  // Open
  {
    auto* row = new QHBoxLayout;
    row->setSpacing(8);
    targetEdit_ = new QLineEdit;
    targetEdit_->setPlaceholderText(QStringLiteral("https://…  or  ~/Documents  or  a file"));
    targetEdit_->setClearButtonEnabled(true);
    auto* file = button(QStringLiteral("File…"));
    auto* folder = button(QStringLiteral("Folder…"));
    row->addWidget(targetEdit_, 1);
    row->addWidget(file);
    row->addWidget(folder);
    addPage(page(row, label(QStringLiteral("Opens a website, file or folder with its default app."), "hint")));
    connect(targetEdit_, &QLineEdit::textEdited, this, [this](const QString& t) {
      cur().target = t.trimmed();
      cur().type = Action::Open;
      actionEdited();
    });
    connect(file, &QPushButton::clicked, this, [this] { browseTarget(false); });
    connect(folder, &QPushButton::clicked, this, [this] { browseTarget(true); });
  }

  bv->addWidget(pagesBox);
  v->addWidget(editorBody_);

  connect(typeGroup_, &QButtonGroup::idClicked, this, [this](int id) {
    setActionType(id);
    if (id == Action::Keys && cur().keys.isEmpty()) recorder_->start();
    if (id == Action::Text) textEdit_->setFocus();
    if (id == Action::Sequence) scriptEdit_->setFocus();
    if (id == Action::Command) cmdEdit_->setFocus();
    if (id == Action::Open) targetEdit_->setFocus();
  });
  connect(triggerGroup_, &QButtonGroup::idClicked, this, [this](int id) {
    recorder_->cancel();
    trigger_ = id;
    refreshEditor();
  });
  connect(labelEdit_, &QLineEdit::textEdited, this, [this](const QString& t) {
    slot(selected_).label = t.trimmed();
    tiles_[selected_]->setSlot(slot(selected_));
    scheduleSave();
  });
  connect(iconBtn_, &QPushButton::clicked, this, [this] { showIconMenu(); });
  return ed;
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------
void MainWindow::refreshPorts() {
  ports_ = plat::listPorts();
  QString keep = link_.isOpen() ? link_.path() : selectedPort();
  if (keep.isEmpty()) keep = cfg_.port;
  QSignalBlocker b(portCombo_);
  portCombo_->clear();
  int sel = -1;
  for (const plat::PortInfo& p : ports_) {
    QString shown = p.path;
#ifndef Q_OS_WIN
    shown = shown.mid(shown.lastIndexOf('/') + 1);
#endif
    if (p.likelyMacropad) shown += QStringLiteral("  ·  macropad");
    else if (!p.description.isEmpty()) shown += QStringLiteral("  ·  ") + p.description;
    portCombo_->addItem(shown, p.path);
    portCombo_->setItemData(portCombo_->count() - 1,
                            p.description.isEmpty() ? p.path : p.path + QStringLiteral(" — ") + p.description,
                            Qt::ToolTipRole);
    if (p.path == keep) sel = portCombo_->count() - 1;
  }
  if (sel < 0 && !keep.isEmpty()) {
    portCombo_->addItem(keep + QStringLiteral("  (not found)"), keep);
    sel = portCombo_->count() - 1;
  }
  if (sel < 0)
    for (size_t i = 0; i < ports_.size(); i++)
      if (ports_[i].likelyMacropad) { sel = (int)i; break; }
  if (portCombo_->count() == 0) portCombo_->addItem(QStringLiteral("No ports found"), QString());
  portCombo_->setCurrentIndex(sel < 0 ? 0 : sel);
}

QString MainWindow::selectedPort() const { return portCombo_->currentData().toString(); }

bool MainWindow::connectTo(const QString& port, bool quiet) {
  if (port.isEmpty()) {
    if (!quiet) setStatus(QStringLiteral("No serial port selected. Plug in the macropad and press ↻."), Warn);
    return false;
  }
  QString err;
  if (!link_.open(port, &err)) {
    updateConnectionUi();
    if (!quiet || err != lastAutoError_) setStatus(err, Error);  // don't repeat the same error every tick
    lastAutoError_ = err;
    return false;
  }
  lastAutoError_.clear();
  pad_.reset();
  cfg_.port = port;
  scheduleSave();
  refreshPorts();
  updateConnectionUi();
  setStatus(QStringLiteral("Connected to %1. Press a key on the macropad.").arg(port), Ok);
  return true;
}

void MainWindow::toggleConnect() {
  if (link_.isOpen()) {
    link_.close();
    userDisconnected_ = true;
    pad_.reset();
    updateConnectionUi();
    setStatus(QStringLiteral("Disconnected."));
  } else {
    userDisconnected_ = false;
    connectTo(selectedPort(), false);
  }
}

// Every 1.5 s while disconnected: reopen the saved port, or anything that looks
// like the macropad (the port name can change after replugging).
void MainWindow::reconnectTick() {
  if (link_.isOpen() || userDisconnected_ || !cfg_.settings.autoReconnect) return;
  const std::vector<plat::PortInfo> now = plat::listPorts();
  bool changed = now.size() != ports_.size();
  for (size_t i = 0; !changed && i < now.size(); i++) changed = now[i].path != ports_[i].path;
  if (changed) refreshPorts();

  QString target;
  for (const plat::PortInfo& p : now)
    if (p.path == cfg_.port) target = p.path;
  if (target.isEmpty())
    for (const plat::PortInfo& p : now)
      if (p.likelyMacropad) { target = p.path; break; }
  if (!target.isEmpty()) connectTo(target, true);
}

void MainWindow::updateConnectionUi() {
  const bool on = link_.isOpen();
  dot_->setStyleSheet(QStringLiteral("background:%1; border-radius:4px;").arg((on ? theme::green : theme::faint).name()));
  QString text = on ? QStringLiteral("Connected") : QStringLiteral("Not connected");
  if (!on && cfg_.settings.autoReconnect && !userDisconnected_) text = QStringLiteral("Waiting for macropad…");
  connLabel_->setText(text);
  connLabel_->setStyleSheet(on ? QString() : QStringLiteral("color:%1;").arg(theme::muted.name()));
  connectBtn_->setText(on ? QStringLiteral("Disconnect") : QStringLiteral("Connect"));
  connectBtn_->setObjectName(on ? QString() : QStringLiteral("primary"));
  theme::repolish(connectBtn_);
  deleteBtn_->setStyleSheet(QStringLiteral("QPushButton:enabled { color:%1; }").arg(theme::danger.name()));
}

void MainWindow::onPadEvent(int n, bool down) {
  const int i = n - 1;
  if (down) tiles_[i]->flash();
  if (recorder_->isRecording()) return;  // don't fire shortcuts into the recorder
  if (down) pad_.press(i);
  else pad_.release(i);
}

void MainWindow::runAction(int key, int trigger, bool fromPad) {
  const Action& a = action(key, trigger);
  QString who = fromPad ? QStringLiteral("Pad key %1").arg(key + 1) : QStringLiteral("Key %1").arg(key + 1);
  if (trigger != Tap) who += QStringLiteral(" (%1)").arg(triggerName(trigger));
  if (!slot(key).label.isEmpty()) who += QStringLiteral(" “%1”").arg(slot(key).label);
  if (!a.isSet()) {
    setStatus(QStringLiteral("%1 isn't assigned in “%2”.").arg(who, cfg_.current().name));
    return;
  }
  QString err;
  if (exec_.run(a, &err)) setStatus(QStringLiteral("%1  →  %2").arg(who, actionSummary(a)));
  else setStatus(err, Error);
}

void MainWindow::testSelected() {
  const Action& a = cur();
  if (!a.isSet()) {
    setStatus(QStringLiteral("Nothing to test yet."), Warn);
    return;
  }
  // Keystrokes would land in this window, so give the user time to switch.
  if (a.type == Action::Keys || a.type == Action::Text || a.type == Action::Sequence) {
    const int key = selected_, trig = trigger_;
    setStatus(QStringLiteral("Running in 3 s. Switch to the window you want to test in…"));
    QTimer::singleShot(3000, this, [this, key, trig] { runAction(key, trig, false); });
  } else {
    runAction(selected_, trigger_, false);
  }
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
void MainWindow::refreshPresetList() {
  QSignalBlocker b(presetList_);
  presetList_->clear();
  for (const Preset& p : cfg_.presets) {
    auto* item = new QListWidgetItem(p.name, presetList_);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
  }
  presetList_->setCurrentRow(cfg_.active);
  deleteBtn_->setEnabled(cfg_.presets.size() > 1);
}

void MainWindow::switchPreset(int index) {
  presetList_->setCurrentRow(index);  // the currentRowChanged handler does the rest
  setStatus(QStringLiteral("Preset  →  %1").arg(cfg_.current().name), Ok);
}

void MainWindow::newPreset() {
  recorder_->cancel();
  Preset p;
  p.name = cfg_.uniqueName(QStringLiteral("New preset"));
  cfg_.presets.push_back(p);
  cfg_.active = (int)cfg_.presets.size() - 1;
  scheduleSave();
  refreshPresetList();
  refreshMain();
  presetList_->editItem(presetList_->item(cfg_.active));
  setStatus(QStringLiteral("Created “%1”. Type a name and press Enter.").arg(p.name), Ok);
}

void MainWindow::duplicatePreset() {
  recorder_->cancel();
  Preset p = cfg_.current();
  p.name = cfg_.uniqueName(p.name + QStringLiteral(" copy"));
  cfg_.presets.insert(cfg_.presets.begin() + cfg_.active + 1, p);
  cfg_.active += 1;
  scheduleSave();
  refreshPresetList();
  refreshMain();
  setStatus(QStringLiteral("Duplicated as “%1”.").arg(p.name), Ok);
}

void MainWindow::deletePreset() {
  if (cfg_.presets.size() <= 1) {
    setStatus(QStringLiteral("You need at least one preset."), Warn);
    return;
  }
  const QString name = cfg_.current().name;
  QMessageBox box(QMessageBox::Question, QStringLiteral("Delete preset"),
                  QStringLiteral("Delete the preset “%1”?").arg(name), QMessageBox::NoButton, this);
  box.setInformativeText(QStringLiteral("All key assignments in it will be lost."));
  auto* del = box.addButton(QStringLiteral("Delete"), QMessageBox::DestructiveRole);
  box.addButton(QMessageBox::Cancel);
  del->setObjectName("primary");
  box.exec();
  if (box.clickedButton() != del) return;

  recorder_->cancel();
  cfg_.presets.erase(cfg_.presets.begin() + cfg_.active);
  if (cfg_.active >= (int)cfg_.presets.size()) cfg_.active = (int)cfg_.presets.size() - 1;
  scheduleSave();
  refreshPresetList();
  refreshMain();
  setStatus(QStringLiteral("Deleted “%1”.").arg(name));
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------
void MainWindow::refreshMain() {
  title_->setText(cfg_.current().name);
  refreshTiles();
  refreshEditor();
}

void MainWindow::refreshTiles() {
  for (int i = 0; i < kKeyCount; i++) {
    tiles_[i]->setSlot(slot(i));
    tiles_[i]->setPresetKey(i == presetKeyIndex());
    tiles_[i]->setSelected(i == selected_);
    tiles_[i]->setRecording(i == recordingKey_);
  }
}

void MainWindow::refreshEditor() {
  if (!editorTitle_) return;
  const KeySlot& s = slot(selected_);
  const Action& a = cur();
  editorTitle_->setText(QStringLiteral("Key %1").arg(selected_ + 1));

  // the preset key has nothing to edit
  const bool isPresetKey = selected_ == presetKeyIndex();
  editorBody_->setVisible(!isPresetKey);
  presetKeyNote_->setVisible(isPresetKey);
  for (QAbstractButton* b : triggerGroup_->buttons()) b->setEnabled(!isPresetKey);
  if (isPresetKey) {
    presetKeyNote_->setText(
        QStringLiteral("Key %1 is the preset key. Hold it and press another key to jump straight to a preset "
                       "(the other keys in order → presets 1–8), or tap it alone for the next preset. "
                       "Change this in Settings ⚙.")
            .arg(selected_ + 1));
  }

  // icon + label
  if (labelEdit_->text() != s.label) labelEdit_->setText(s.label);
  const QString file = Config::resolveIcon(s.icon);
  iconBtn_->setIcon(file.isEmpty() ? QIcon() : QIcon(file));
  iconBtn_->setText(file.isEmpty() ? (s.icon.isEmpty() ? QStringLiteral("+") : s.icon) : QString());
  iconBtn_->setStyleSheet(s.icon.isEmpty() ? QStringLiteral("color:%1;").arg(theme::faint.name()) : QString());

  // trigger tabs show a dot when that trigger has an action
  const char* names[] = {"Tap", "Hold", "Double-tap"};
  for (int t = 0; t < TriggerCount; t++) {
    QAbstractButton* b = triggerGroup_->button(t);
    b->setText(QString::fromLatin1(names[t]) + (t != Tap && s.act[t].isSet() ? QStringLiteral("  •") : QString()));
    b->setChecked(t == trigger_);
  }
  QString th;
  if (!isPresetKey && trigger_ == Hold) {
    th = QStringLiteral("Runs when the key is held for %1 ms. With a hold action, the tap action fires on release "
                        "instead of on press.")
             .arg(cfg_.settings.holdMs);
    if (link_.isOpen() && !pad_.releasesSeen())
      th += QStringLiteral(" Needs the updated firmware that reports key releases.");
  } else if (!isPresetKey && trigger_ == DoubleTap) {
    th = QStringLiteral("Runs on two quick presses (within %1 ms). With a double-tap action, a single tap waits "
                        "that long before firing.")
             .arg(cfg_.settings.doubleMs);
  }
  triggerHint_->setText(th);
  triggerHint_->setVisible(!th.isEmpty());

  // action
  typeGroup_->button(a.type)->setChecked(true);
  showPage(a.type);
  recorder_->setKeys(a.keys);
  if (textEdit_->toPlainText() != a.text) {
    QSignalBlocker b(textEdit_);
    textEdit_->setPlainText(a.text);
  }
  if (scriptEdit_->toPlainText() != a.script) {
    QSignalBlocker b(scriptEdit_);
    scriptEdit_->setPlainText(a.script);
  }
  validateScript();
  if (cmdEdit_->text().trimmed() != a.command) {
    QSignalBlocker b(cmdEdit_);
    cmdEdit_->setText(a.command);
  }
  if (targetEdit_->text().trimmed() != a.target) {
    QSignalBlocker b(targetEdit_);
    targetEdit_->setText(a.target);
  }
}

void MainWindow::selectKey(int i) {
  if (i != selected_) recorder_->cancel();
  selected_ = i;
  refreshTiles();
  refreshEditor();
}

void MainWindow::setActionType(int type) {
  if (cur().type != type) {
    cur().type = type;
    scheduleSave();
  }
  if (type != Action::Keys) recorder_->cancel();
  refreshTiles();
  refreshEditor();
}

// After a field of the selected action was edited: save and update everything
// except the field being typed in (so the cursor doesn't jump).
void MainWindow::actionEdited() {
  scheduleSave();
  tiles_[selected_]->setSlot(slot(selected_));
  const char* names[] = {"Tap", "Hold", "Double-tap"};
  for (int t = 1; t < TriggerCount; t++)
    triggerGroup_->button(t)->setText(QString::fromLatin1(names[t]) +
                                      (slot(selected_).act[t].isSet() ? QStringLiteral("  •") : QString()));
  typeGroup_->button(cur().type)->setChecked(true);
  recorder_->setKeys(cur().keys);
  showPage(cur().type);
}

void MainWindow::validateScript() {
  if (!scriptMsg_) return;
  QString err;
  std::vector<Step> steps;
  const QString script = scriptEdit_->toPlainText();
  const bool ok = script.trimmed().isEmpty() || parseScript(script, &steps, &err);
  scriptMsg_->setObjectName(ok ? QStringLiteral("hint") : QStringLiteral("error"));
  scriptMsg_->setText(ok ? QStringLiteral("One step per line:  keys Ctrl+S  ·  text Hello\\n  ·  wait 200  ·  "
                                          "run <command>  ·  open <url or path>  ·  # comment")
                         : err);
  theme::repolish(scriptMsg_);
}

void MainWindow::insertScriptLine(const QString& line) {
  QTextCursor c = scriptEdit_->textCursor();
  c.movePosition(QTextCursor::EndOfLine);
  if (!c.block().text().trimmed().isEmpty()) c.insertText(QStringLiteral("\n"));
  c.insertText(line);
  scriptEdit_->setTextCursor(c);
  scriptEdit_->setFocus();
}

void MainWindow::showKeyMenu(int i, const QPoint& globalPos) {
  selectKey(i);
  if (i == presetKeyIndex()) return;
  QMenu m(this);
  QAction* rec = m.addAction(QStringLiteral("Record shortcut"));
  QAction* text = m.addAction(QStringLiteral("Type text…"));
  QAction* seq = m.addAction(QStringLiteral("Sequence…"));
  QAction* cmd = m.addAction(QStringLiteral("Run a command…"));
  QAction* open = m.addAction(QStringLiteral("Open a file, folder or URL…"));
  m.addSeparator();
  QAction* test = m.addAction(QStringLiteral("Test (tap action)"));
  test->setEnabled(action(i, Tap).isSet());
  QAction* clr = m.addAction(QStringLiteral("Clear key"));
  QAction* chosen = m.exec(globalPos);
  if (!chosen) return;
  if (chosen == clr) {
    slot(i) = KeySlot();
    scheduleSave();
    refreshTiles();
    refreshEditor();
    setStatus(QStringLiteral("Cleared key %1.").arg(i + 1));
    return;
  }
  trigger_ = Tap;
  if (chosen == test) {
    refreshEditor();
    testSelected();
  } else if (chosen == rec) {
    setActionType(Action::Keys);
    recorder_->start();
  } else if (chosen == text) {
    setActionType(Action::Text);
    textEdit_->setFocus();
  } else if (chosen == seq) {
    setActionType(Action::Sequence);
    scriptEdit_->setFocus();
  } else if (chosen == cmd) {
    setActionType(Action::Command);
    cmdEdit_->setFocus();
  } else if (chosen == open) {
    setActionType(Action::Open);
    targetEdit_->setFocus();
  }
}

void MainWindow::showIconMenu() {
  QMenu m(this);
  QAction* img = m.addAction(QStringLiteral("Choose image…"));
  QAction* emo = m.addAction(QStringLiteral("Emoji or text…"));
  m.addSeparator();
  QAction* rm = m.addAction(QStringLiteral("Remove icon"));
  rm->setEnabled(!slot(selected_).icon.isEmpty());
  QAction* chosen = m.exec(iconBtn_->mapToGlobal(QPoint(0, iconBtn_->height() + 4)));
  KeySlot& s = slot(selected_);
  if (chosen == img) {
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose an icon"), QString(),
                                                      QStringLiteral("Images (*.png *.svg *.jpg *.jpeg *.ico *.webp)"));
    if (file.isEmpty()) return;
    // copy into the config dir so presets keep working if the original moves
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
      setStatus(QStringLiteral("Could not read %1.").arg(file), Error);
      return;
    }
    const QByteArray data = in.readAll();
    const QString name = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16)) +
                         '.' + QFileInfo(file).suffix().toLower();
    QDir().mkpath(Config::dir() + QStringLiteral("/icons"));
    QFile out(Config::dir() + QStringLiteral("/icons/") + name);
    if (!out.exists() && (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size())) {
      setStatus(QStringLiteral("Could not save the icon."), Error);
      return;
    }
    s.icon = QStringLiteral("file:icons/") + name;
  } else if (chosen == emo) {
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Key icon"),
                                            QStringLiteral("Emoji or up to 3 characters (e.g. 🎵, ⏯, ★, Fx):"),
                                            QLineEdit::Normal,
                                            Config::resolveIcon(s.icon).isEmpty() ? s.icon : QString(), &ok)
                          .trimmed();
    if (!ok) return;
    s.icon = t.left(6);
  } else if (chosen == rm) {
    s.icon.clear();
  } else {
    return;
  }
  scheduleSave();
  refreshTiles();
  refreshEditor();
}

void MainWindow::browseCommand() {
#ifdef Q_OS_WIN
  const QString filter = QStringLiteral("Programs and scripts (*.exe *.bat *.cmd *.lnk *.ps1);;All files (*)");
#else
  const QString filter = QStringLiteral("All files (*);;Scripts (*.sh *.py)");
#endif
  const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a program or script"), QString(), filter);
  if (file.isEmpty()) return;
  cur().command = QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(file));
  cur().type = Action::Command;
  scheduleSave();
  refreshTiles();
  refreshEditor();
}

void MainWindow::browseTarget(bool folder) {
  const QString path = folder ? QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder"))
                              : QFileDialog::getOpenFileName(this, QStringLiteral("Choose a file"));
  if (path.isEmpty()) return;
  cur().target = QDir::toNativeSeparators(path);
  cur().type = Action::Open;
  scheduleSave();
  refreshTiles();
  refreshEditor();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void MainWindow::openSettings() {
  QDialog d(this);
  d.setWindowTitle(QStringLiteral("Settings"));
  d.setMinimumWidth(480);
  auto* v = new QVBoxLayout(&d);
  v->setContentsMargins(22, 20, 22, 18);
  v->setSpacing(14);

  auto* form = new QFormLayout;
  form->setHorizontalSpacing(18);
  form->setVerticalSpacing(12);
  form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  auto* themeBox = new QComboBox;
  themeBox->addItem(QStringLiteral("Match system"), QStringLiteral("system"));
  themeBox->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
  themeBox->addItem(QStringLiteral("Light"), QStringLiteral("light"));
  themeBox->setCurrentIndex(qMax(0, themeBox->findData(cfg_.settings.theme)));
  form->addRow(QStringLiteral("Appearance"), themeBox);

  auto* hold = new QSpinBox;
  hold->setRange(150, 2000);
  hold->setSingleStep(50);
  hold->setSuffix(QStringLiteral(" ms"));
  hold->setButtonSymbols(QAbstractSpinBox::NoButtons);
  hold->setToolTip(QStringLiteral("Type a value or scroll"));
  hold->setValue(cfg_.settings.holdMs);
  form->addRow(QStringLiteral("Hold after"), hold);

  auto* dbl = new QSpinBox;
  dbl->setRange(100, 800);
  dbl->setSingleStep(25);
  dbl->setSuffix(QStringLiteral(" ms"));
  dbl->setButtonSymbols(QAbstractSpinBox::NoButtons);
  dbl->setToolTip(QStringLiteral("Type a value or scroll"));
  dbl->setValue(cfg_.settings.doubleMs);
  form->addRow(QStringLiteral("Double-tap within"), dbl);

  auto* pk = new QComboBox;
  pk->addItem(QStringLiteral("Off"), 0);
  for (int i = 1; i <= kKeyCount; i++) pk->addItem(QStringLiteral("Key %1").arg(i), i);
  pk->setCurrentIndex(cfg_.settings.presetKey);
  form->addRow(QStringLiteral("Preset key"), pk);

  auto* recon = new QCheckBox(QStringLiteral("Reconnect automatically"));
  recon->setToolTip(QStringLiteral("Connect as soon as the macropad is plugged in"));
  recon->setChecked(cfg_.settings.autoReconnect);
  form->addRow(QString(), recon);
  v->addLayout(form);

  auto* note = label(QStringLiteral("The preset key is reserved: hold it and press another key to jump to a preset "
                                    "(the other keys in order → presets 1–8), or tap it for the next preset. "
                                    "Holding needs the firmware that reports key releases."),
                     "hint");
  note->setWordWrap(true);
  v->addWidget(note);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  v->addWidget(buttons);

  auto changed = [this] {
    pad_.holdMs = cfg_.settings.holdMs;
    pad_.doubleMs = cfg_.settings.doubleMs;
    pad_.presetKey = presetKeyIndex();
    pad_.reset();
    scheduleSave();
    refreshTiles();
    refreshEditor();
    updateConnectionUi();
  };
  connect(themeBox, &QComboBox::currentIndexChanged, this, [this, themeBox, changed](int) {
    cfg_.settings.theme = themeBox->currentData().toString();
    theme::apply(app(), cfg_.settings.theme);
    changed();
  });
  connect(hold, &QSpinBox::valueChanged, this, [this, changed](int ms) {
    cfg_.settings.holdMs = ms;
    changed();
  });
  connect(dbl, &QSpinBox::valueChanged, this, [this, changed](int ms) {
    cfg_.settings.doubleMs = ms;
    changed();
  });
  connect(pk, &QComboBox::currentIndexChanged, this, [this, changed](int i) {
    recorder_->cancel();
    cfg_.settings.presetKey = i;
    changed();
  });
  connect(recon, &QCheckBox::toggled, this, [this, changed](bool on) {
    cfg_.settings.autoReconnect = on;
    if (on) userDisconnected_ = false;
    changed();
  });
  d.exec();
}

// ---------------------------------------------------------------------------
void MainWindow::scheduleSave() { saveTimer_->start(); }

void MainWindow::setStatus(const QString& text, StatusKind kind) {
  lastStatus_ = text;
  lastStatusKind_ = kind;
  if (!status_) return;
  const QColor c = kind == Ok ? theme::green : kind == Warn ? theme::warn : kind == Error ? theme::danger : theme::muted;
  status_->setText(QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp;&nbsp;<span style='color:%3'>%4</span>")
                       .arg(theme::faint.name(), QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), c.name(),
                            text.toHtmlEscaped()));
}
