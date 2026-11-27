// Custom-painted widgets: the keycap tiles of the 3x3 grid and the shortcut
// recorder field.
#pragma once

#include <QStringList>
#include <QWidget>
#include <functional>

#include "config.h"

class QVariantAnimation;

// Paints a key icon: emoji/short text, or an image for "file:" icons.
void drawKeyIcon(QPainter& p, const QRectF& r, const QString& icon);

// Draws key ids as little keycaps ("Ctrl" + "C"). Returns false if they did not
// fit and plain elided text was drawn instead.
bool drawChips(QPainter& p, const QRectF& area, const QStringList& ids, Qt::Alignment align, QFont font);

class KeyTile : public QWidget {
 public:
  explicit KeyTile(int index, QWidget* parent = nullptr);

  void setSlot(const KeySlot& s);
  void setPresetKey(bool on);
  void setSelected(bool on);
  void setRecording(bool on);
  void flash();

  std::function<void(int)> onClick;
  std::function<void(int)> onDoubleClick;
  std::function<void(int, QPoint)> onContextMenu;  // global position

  QSize sizeHint() const override { return {180, 116}; }
  QSize minimumSizeHint() const override { return {140, 96}; }

 protected:
  void paintEvent(QPaintEvent*) override;
  void enterEvent(QEnterEvent*) override;
  void leaveEvent(QEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void contextMenuEvent(QContextMenuEvent*) override;

 private:
  int idx_;
  KeySlot slot_;
  bool presetKey_ = false;
  bool sel_ = false, rec_ = false, hover_ = false, down_ = false;
  qreal flash_ = 0;
  QVariantAnimation* anim_;
};

class ShortcutRecorder : public QWidget {
 public:
  explicit ShortcutRecorder(QWidget* parent = nullptr);

  void setKeys(const QStringList& keys);
  bool isRecording() const { return rec_; }
  void start();
  void cancel();

  std::function<void()> onStarted;
  std::function<void(const QStringList&)> onRecorded;
  std::function<void()> onCancelled;
  std::function<void(const QString&)> onMessage;

  QSize sizeHint() const override { return {320, 50}; }

 protected:
  bool event(QEvent* e) override;
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void keyReleaseEvent(QKeyEvent*) override;
  void focusOutEvent(QFocusEvent*) override;

 private:
  void finish();
  QStringList keys_, held_, combo_;
  bool rec_ = false;
};
