#include "widgets.h"

#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QPixmapCache>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include "actions.h"
#include "keys.h"
#include "theme.h"

static QColor mix(const QColor& a, const QColor& b, qreal t) {
  return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                          a.blueF() + (b.blueF() - a.blueF()) * t);
}

bool drawChips(QPainter& p, const QRectF& area, const QStringList& ids, Qt::Alignment align, QFont font) {
  if (ids.isEmpty()) return true;
  for (int attempt = 0; attempt < 3; attempt++) {
    const QFontMetricsF fm(font);
    const qreal h = fm.height() + 10, padX = 9, gap = 6, plusW = fm.horizontalAdvance('+');
    QList<qreal> widths;
    qreal total = 0;
    for (const QString& id : ids) {
      const qreal w = qMax(h, fm.horizontalAdvance(keyLabel(id)) + 2 * padX);
      widths << w;
      total += w;
    }
    total += (ids.size() - 1) * (2 * gap + plusW);

    if (total > area.width()) {
      if (attempt < 2) {
        font.setPointSizeF(font.pointSizeF() * 0.86);
        continue;
      }
      p.setFont(font);
      p.setPen(theme::text);
      p.drawText(area, (align & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter,
                 fm.elidedText(comboText(ids), Qt::ElideRight, area.width()));
      return false;
    }

    qreal x = (align & Qt::AlignHCenter) ? area.center().x() - total / 2 : area.left();
    const qreal y = area.center().y() - (h + 2) / 2;
    p.setFont(font);
    for (int i = 0; i < ids.size(); i++) {
      const QRectF face(x, y, widths[i], h);
      QPainterPath lipPath, facePath;
      lipPath.addRoundedRect(face.translated(0, 2), 6, 6);
      facePath.addRoundedRect(face, 6, 6);
      p.fillPath(lipPath, theme::lip);
      p.fillPath(facePath, theme::chip);
      p.setPen(QPen(theme::chipBorder, 1));
      p.drawPath(facePath);
      p.setPen(theme::text);
      p.drawText(face, Qt::AlignCenter, keyLabel(ids[i]));
      x += widths[i];
      if (i + 1 < ids.size()) {
        p.setPen(theme::faint);
        p.drawText(QRectF(x, y, 2 * gap + plusW, h), Qt::AlignCenter, QStringLiteral("+"));
        x += 2 * gap + plusW;
      }
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// KeyTile
// ---------------------------------------------------------------------------
KeyTile::KeyTile(int index, QWidget* parent) : QWidget(parent), idx_(index) {
  setCursor(Qt::PointingHandCursor);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  anim_ = new QVariantAnimation(this);
  anim_->setDuration(520);
  anim_->setEasingCurve(QEasingCurve::OutCubic);
  QObject::connect(anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
    flash_ = v.toReal();
    update();
  });
}

void KeyTile::setSlot(const KeySlot& s) {
  slot_ = s;
  update();
}
void KeyTile::setPresetKey(bool on) {
  if (presetKey_ != on) { presetKey_ = on; update(); }
}
void KeyTile::setSelected(bool on) {
  if (sel_ != on) { sel_ = on; update(); }
}
void KeyTile::setRecording(bool on) {
  if (rec_ != on) { rec_ = on; update(); }
}
void KeyTile::flash() {
  anim_->stop();
  anim_->setStartValue(1.0);
  anim_->setEndValue(0.0);
  anim_->start();
}

void KeyTile::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::TextAntialiasing);

  const qreal lipH = 5, rad = 14;
  const qreal press = (down_ ? 3 : 0) + flash_ * 3;
  const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);
  const QRectF face(outer.x(), outer.y() + press, outer.width(), outer.height() - lipH);

  // keycap lip (the "side" of the key)
  QPainterPath lipPath;
  lipPath.addRoundedRect(outer.adjusted(0, lipH, 0, 0), rad, rad);
  p.fillPath(lipPath, theme::lip);

  QColor fill = hover_ && !down_ ? theme::cardHover : theme::card;
  if (sel_ || rec_) fill = mix(fill, theme::accentBg, 0.55);
  if (flash_ > 0) fill = mix(fill, theme::accent, flash_ * 0.55);
  QColor edge = sel_ || rec_ ? theme::accent : (hover_ ? theme::borderHi : theme::border);
  if (flash_ > 0) edge = mix(edge, theme::accentHi, flash_);
  const qreal bw = sel_ || rec_ ? 2.0 : 1.0;

  QPainterPath facePath;
  facePath.addRoundedRect(face.adjusted(bw / 2, bw / 2, -bw / 2, -bw / 2), rad, rad);
  p.fillPath(facePath, fill);
  QPen pen(edge, bw);
  if (rec_) pen.setStyle(Qt::DashLine);
  p.setPen(pen);
  p.drawPath(facePath);

  const Action& tap = slot_.act[Tap];
  const qreal base = font().pointSizeF();

  // header: key number + type badge
  QFont small = font();
  small.setPointSizeF(base * 0.78);
  small.setBold(true);
  small.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
  p.setFont(small);
  const QRectF head = face.adjusted(14, 11, -14, 0);
  p.setPen(sel_ ? theme::accentHi : theme::faint);
  p.drawText(head, Qt::AlignLeft | Qt::AlignTop, QStringLiteral("KEY %1").arg(idx_ + 1));

  QString badge;
  QColor badgeCol = theme::faint;
  if (rec_) { badge = QStringLiteral("RECORDING"); badgeCol = theme::accent; }
  else if (presetKey_) { badge = QStringLiteral("PRESET KEY"); badgeCol = theme::accent; }
  else if (tap.isSet()) {
    badge = actionTypeName(tap.type).toUpper();
    if (tap.type != Action::Keys) badgeCol = theme::accent;
  }
  if (!badge.isEmpty()) {
    p.setPen(badgeCol);
    p.drawText(head, Qt::AlignRight | Qt::AlignTop, badge);
  }

  // footer: extra triggers
  QStringList extras;
  if (!presetKey_ && slot_.act[Hold].isSet()) extras << QStringLiteral("HOLD");
  if (!presetKey_ && slot_.act[DoubleTap].isSet()) extras << QStringLiteral("DOUBLE-TAP");
  const qreal footH = extras.isEmpty() ? 0 : 16;
  if (footH > 0) {
    QFont ff = small;
    ff.setPointSizeF(base * 0.7);
    p.setFont(ff);
    p.setPen(theme::muted);
    p.drawText(face.adjusted(14, 0, -14, -10), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("+ ") + extras.join(QStringLiteral("  ·  ")));
  }

  // body
  const QRectF body = face.adjusted(12, 28, -12, -(10 + footH));
  QFont f = font();
  auto centered = [&](const QString& t, const QColor& c, const QFont& fnt) {
    p.setFont(fnt);
    p.setPen(c);
    p.drawText(body, Qt::AlignCenter, QFontMetricsF(fnt).elidedText(t, Qt::ElideMiddle, body.width()));
  };

  if (rec_) {
    QFont rf = f;
    rf.setPointSizeF(base * 1.05);
    centered(QStringLiteral("Press keys…"), theme::accentHi, rf);
  } else if (presetKey_) {
    QFont sf = f;
    sf.setPointSizeF(base * 0.9);
    p.setFont(sf);
    p.setPen(theme::muted);
    p.drawText(body, Qt::AlignCenter, QStringLiteral("Hold + key → preset\nTap → next preset"));
  } else if (!slot_.label.isEmpty() || !slot_.icon.isEmpty()) {
    // icon + label on one row, action summary underneath
    QFont lf = f;
    lf.setPointSizeF(base * 1.08);
    lf.setWeight(QFont::DemiBold);
    QFont sf = f;
    sf.setPointSizeF(base * 0.85);
    const QFontMetricsF lfm(lf), sfm(sf);
    const QString summary = actionSummary(tap);
    const bool hasIcon = !slot_.icon.isEmpty(), hasLabel = !slot_.label.isEmpty();
    const qreal iconSz = qMin<qreal>(32, body.height() * 0.58);
    const qreal rowH = qMax(hasIcon ? iconSz : 0.0, hasLabel ? lfm.height() : 0.0);
    const qreal sumH = summary.isEmpty() ? 0 : sfm.height() + 4;
    const qreal top = body.center().y() - (rowH + sumH) / 2;
    const qreal gap = hasIcon && hasLabel ? 8 : 0;
    const QString label = lfm.elidedText(slot_.label, Qt::ElideRight, body.width() - (hasIcon ? iconSz + gap : 0));
    const qreal rowW = (hasIcon ? iconSz : 0) + gap + (hasLabel ? lfm.horizontalAdvance(label) : 0);
    qreal x = body.center().x() - rowW / 2;
    if (hasIcon) {
      drawKeyIcon(p, QRectF(x, top + (rowH - iconSz) / 2, iconSz, iconSz), slot_.icon);
      x += iconSz + gap;
    }
    if (hasLabel) {
      p.setFont(lf);
      p.setPen(theme::text);
      p.drawText(QRectF(x, top, body.right() - x, rowH), Qt::AlignLeft | Qt::AlignVCenter, label);
    }
    if (!summary.isEmpty()) {
      p.setFont(sf);
      p.setPen(theme::muted);
      p.drawText(QRectF(body.left(), top + rowH + 4, body.width(), sfm.height()), Qt::AlignCenter,
                 sfm.elidedText(summary, Qt::ElideMiddle, body.width()));
    }
  } else if (tap.isSet() && tap.type == Action::Keys) {
    drawChips(p, body, tap.keys, Qt::AlignHCenter, f);
  } else if (tap.isSet() && tap.type == Action::Command) {
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(base * 0.95);
    p.setFont(mono);
    const QFontMetricsF fm(mono);
    const QString prompt = QStringLiteral("$ ");
    const qreal pw = fm.horizontalAdvance(prompt);
    const QString cmd = fm.elidedText(tap.command, Qt::ElideMiddle, body.width() - pw);
    const qreal tw = pw + fm.horizontalAdvance(cmd);
    const QRectF line(body.center().x() - tw / 2, body.top(), tw, body.height());
    p.setPen(theme::accent);
    p.drawText(line, Qt::AlignLeft | Qt::AlignVCenter, prompt);
    p.setPen(theme::text);
    p.drawText(line.adjusted(pw, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, cmd);
  } else if (tap.isSet()) {
    const QString glyph = tap.type == Action::Sequence ? QStringLiteral("▶  ")
                          : tap.type == Action::Open   ? QStringLiteral("↗  ")
                                                       : QString();
    centered(glyph + actionSummary(tap), theme::text, f);
  } else {
    centered(extras.isEmpty() ? QStringLiteral("Not assigned") : QStringLiteral("Tap: nothing"),
             hover_ ? theme::muted : theme::faint, f);
  }
}

void drawKeyIcon(QPainter& p, const QRectF& r, const QString& icon) {
  if (icon.isEmpty()) return;
  const QString file = Config::resolveIcon(icon);
  if (!file.isEmpty()) {
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QString key = file + QString::number(int(r.width() * dpr));
    QPixmap pm;
    if (!QPixmapCache::find(key, &pm)) {
      pm = QIcon(file).pixmap(QSize(int(r.width() * dpr), int(r.height() * dpr)));
      QPixmapCache::insert(key, pm);
    }
    if (!pm.isNull()) {
      const QSizeF sz = QSizeF(pm.size()) / dpr;
      p.drawPixmap(QRectF(r.center().x() - sz.width() / 2, r.center().y() - sz.height() / 2, sz.width(), sz.height()),
                   pm, QRectF(QPointF(0, 0), QSizeF(pm.size())));
    }
    return;
  }
  QFont f = p.font();
  f.setPixelSize(qMax(8, int(r.height())));
  f.setBold(false);
  p.setFont(f);
  p.setPen(theme::text);
  p.drawText(r, Qt::AlignCenter, icon);
}

void KeyTile::enterEvent(QEnterEvent*) { hover_ = true; update(); }
void KeyTile::leaveEvent(QEvent*) { hover_ = false; down_ = false; update(); }

void KeyTile::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) { down_ = true; update(); }
}
void KeyTile::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const bool wasDown = down_;
  down_ = false;
  update();
  if (wasDown && rect().contains(e->position().toPoint()) && onClick) onClick(idx_);
}
void KeyTile::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && onDoubleClick) onDoubleClick(idx_);
}
void KeyTile::contextMenuEvent(QContextMenuEvent* e) {
  if (onContextMenu) onContextMenu(idx_, e->globalPos());
}

// ---------------------------------------------------------------------------
// ShortcutRecorder
// ---------------------------------------------------------------------------
ShortcutRecorder::ShortcutRecorder(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::PointingHandCursor);
  setMinimumHeight(50);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ShortcutRecorder::setKeys(const QStringList& keys) {
  keys_ = keys;
  update();
}

void ShortcutRecorder::start() {
  if (rec_) return;
  rec_ = true;
  held_.clear();
  combo_.clear();
  setFocus(Qt::OtherFocusReason);
  update();
  if (onStarted) onStarted();
}

void ShortcutRecorder::cancel() {
  if (!rec_) return;
  rec_ = false;
  update();
  if (onCancelled) onCancelled();
}

void ShortcutRecorder::finish() {
  rec_ = false;
  keys_ = sortCombo(combo_);
  update();
  if (onRecorded) onRecorded(keys_);
}

bool ShortcutRecorder::event(QEvent* e) {
  if (rec_) {
    // Keep Tab, Alt+letter, app shortcuts etc. from being eaten before we see them.
    if (e->type() == QEvent::ShortcutOverride) {
      e->accept();
      return true;
    }
    if (e->type() == QEvent::KeyPress) {
      keyPressEvent(static_cast<QKeyEvent*>(e));
      return true;
    }
  }
  return QWidget::event(e);
}

void ShortcutRecorder::keyPressEvent(QKeyEvent* e) {
  if (!rec_) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) start();
    else QWidget::keyPressEvent(e);
    return;
  }
  if (e->isAutoRepeat()) return;
  if (e->key() == Qt::Key_Escape && held_.isEmpty()) {
    cancel();
    return;
  }
  const KeyDef* k = keyFromEvent(e);
  if (!k) {
    if (onMessage) onMessage(QStringLiteral("That key isn't supported yet."));
    return;
  }
  const QString id = QString::fromLatin1(k->id);
  if (!held_.contains(id)) held_ << id;
  if (!combo_.contains(id)) combo_ << id;
  update();
  if (!k->modifier) finish();  // a normal key ends the shortcut
}

void ShortcutRecorder::keyReleaseEvent(QKeyEvent* e) {
  if (!rec_ || e->isAutoRepeat()) return;
  if (const KeyDef* k = keyFromEvent(e)) held_.removeAll(QString::fromLatin1(k->id));
  // only modifiers were pressed (e.g. just "Ctrl" or "Ctrl+Shift") and all are released now
  if (held_.isEmpty() && !combo_.isEmpty()) finish();
}

void ShortcutRecorder::focusOutEvent(QFocusEvent* e) {
  QWidget::focusOutEvent(e);
  cancel();
  update();
}

void ShortcutRecorder::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) start();
}

void ShortcutRecorder::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
  QPainterPath path;
  path.addRoundedRect(r, 10, 10);
  p.fillPath(path, rec_ ? theme::accentBg : theme::field);
  QPen pen(rec_ ? theme::accent : (hasFocus() ? theme::borderHi : theme::border), rec_ ? 1.6 : 1.0);
  p.setPen(pen);
  p.drawPath(path);

  const QRectF inner = r.adjusted(14, 0, -14, 0);
  QFont f = font();
  const QString right = rec_ ? QStringLiteral("Esc to cancel") : QStringLiteral("Click to record");
  QFont hintFont = font();
  hintFont.setPointSizeF(font().pointSizeF() * 0.9);
  const qreal rw = QFontMetricsF(hintFont).horizontalAdvance(right) + 8;
  const QRectF left = inner.adjusted(0, 0, -rw, 0);

  if (rec_ && combo_.isEmpty()) {
    p.setFont(f);
    p.setPen(theme::accentHi);
    p.drawText(left, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Press a key or a combination…"));
  } else if (rec_) {
    drawChips(p, left, sortCombo(combo_), Qt::AlignLeft, f);
  } else if (keys_.isEmpty()) {
    p.setFont(f);
    p.setPen(theme::faint);
    p.drawText(left, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("No shortcut yet"));
  } else {
    drawChips(p, left, keys_, Qt::AlignLeft, f);
  }

  p.setFont(hintFont);
  p.setPen(rec_ ? theme::muted : theme::faint);
  p.drawText(inner, Qt::AlignRight | Qt::AlignVCenter, right);
}
