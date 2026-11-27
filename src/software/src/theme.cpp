#include "theme.h"

#include <QApplication>
#include <QIcon>
#include <QMap>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace theme {

// @name@ placeholders are replaced with the current palette.
static const char* kStyle = R"(
QFrame#sidebar { background: @sidebar@; border-right: 1px solid @line@; }
QFrame#main    { background: @bg@; }
QDialog        { background: @bg@; }

QLabel#brand      { font-size: 14pt; font-weight: 600; }
QLabel#brandSub   { font-size: 14pt; color: @muted@; }
QLabel#section    { color: @faint@; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLabel#title      { font-size: 21pt; font-weight: 600; }
QLabel#muted      { color: @muted@; }
QLabel#hint       { color: @faint@; }
QLabel#error      { color: @danger@; }
QLabel#editorTitle{ font-size: 13pt; font-weight: 600; }
QLabel#status     { color: @muted@; padding-top: 2px; }
QLabel#warning    { background: @warnBg@; border: 1px solid @warnBorder@; border-radius: 10px;
                    padding: 10px 12px; color: @warnText@; }

QFrame#card   { background: @bg@; border: 1px solid @line@; border-radius: 12px; }
QFrame#editor { background: @panel@; border: 1px solid @line@; border-radius: 16px; }
QFrame#rule   { background: @line@; max-height: 1px; border: none; }

QPushButton {
  background: @card@; border: 1px solid @border@; border-radius: 8px;
  padding: 7px 14px; color: @text@;
}
QPushButton:hover    { background: @cardHover@; border-color: @borderHi@; }
QPushButton:pressed  { background: @field@; }
QPushButton:disabled { color: @faint@; border-color: @line@; }
QPushButton#primary  { background: @accent@; border-color: @accent@; color: @onAccent@; font-weight: 600; }
QPushButton#primary:hover   { background: @accentHi@; border-color: @accentHi@; }
QPushButton#danger   { color: @danger@; }
QPushButton#small    { padding: 7px 6px; }
QPushButton#ghost    { background: transparent; border-color: transparent; color: @muted@; padding: 6px 10px; }
QPushButton#ghost:hover { background: @hover@; color: @text@; }
QPushButton#iconBtn  { padding: 0; font-size: 17pt; border-radius: 10px; background: @field@; }
QPushButton#iconBtn:hover { border-color: @accent@; }
QPushButton::menu-indicator { subcontrol-position: right center; right: 8px; }

QFrame#segment { background: @segment@; border: 1px solid @line@; border-radius: 10px; }
QPushButton#seg { background: transparent; border: none; border-radius: 7px; padding: 6px 12px; color: @muted@; }
QPushButton#seg:hover   { color: @text@; }
QPushButton#seg:checked { background: @segOn@; color: @text@; font-weight: 600; }
QPushButton#seg:disabled { color: @faint@; }

QComboBox, QSpinBox {
  background: @field@; border: 1px solid @border@; border-radius: 8px;
  padding: 6px 10px; color: @text@;
}
QComboBox:hover, QSpinBox:hover { border-color: @borderHi@; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
  background: @card@; border: 1px solid @border@; outline: 0; padding: 4px; color: @text@;
  selection-background-color: @accentBg@; selection-color: @text@;
}

QLineEdit, QPlainTextEdit {
  background: @field@; border: 1px solid @border@; border-radius: 8px;
  padding: 8px 10px; color: @text@; selection-background-color: @accent@; selection-color: @onAccent@;
}
QLineEdit:hover, QPlainTextEdit:hover { border-color: @borderHi@; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: @accent@; }

QCheckBox { spacing: 8px; color: @text@; }

QListWidget#presets { background: transparent; border: none; outline: 0; }
QListWidget#presets::item { padding: 8px 10px; border-radius: 8px; margin: 1px 0; color: @text@; }
QListWidget#presets::item:hover    { background: @hover@; }
QListWidget#presets::item:selected { background: @accentBg@; color: @text@; }
QListWidget#presets QLineEdit { padding: 2px 6px; border-radius: 6px; }

QMenu { background: @card@; border: 1px solid @border@; padding: 5px; }
QMenu::item { padding: 7px 22px 7px 14px; border-radius: 6px; color: @text@; }
QMenu::item:selected { background: @accentBg@; }
QMenu::item:disabled { color: @faint@; }
QMenu::separator { height: 1px; background: @line@; margin: 5px 8px; }

QToolTip { background: @card@; color: @text@; border: 1px solid @border@; padding: 5px 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @border@; border-radius: 3px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: @borderHi@; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)";

static bool g_dark = true;
static QString g_mode;
static std::function<void()> g_onChanged;

static void setPalette(bool dark) {
  auto c = [](const char* hex) { return QColor(QString::fromLatin1(hex)); };
  if (dark) {
    bg = c("#262624"); sidebar = c("#1E1E1C"); card = c("#30302E"); cardHover = c("#373734");
    lip = c("#181817"); border = c("#464541"); borderHi = c("#62605A"); text = c("#FAF9F5");
    muted = c("#AAA8A0"); faint = c("#75736C"); accent = c("#D97757"); accentHi = c("#E88A6A");
    accentBg = c("#422F28"); danger = c("#EE7060"); green = c("#6EBE78"); warn = c("#E8A06A");
    chip = c("#41403C"); chipBorder = c("#585650"); field = c("#232321"); panel = c("#2C2C2A");
    segment = c("#1E1E1C"); segOn = c("#3A3A37"); line = c("#33322F"); hover = c("#2A2A27");
    onAccent = c("#261812"); warnBg = c("#3A2620"); warnBorder = c("#6B3F2E"); warnText = c("#F2C4B3");
  } else {
    bg = c("#F4F3EE"); sidebar = c("#EBE9E2"); card = c("#FFFFFF"); cardHover = c("#FBFAF7");
    lip = c("#D3CFC5"); border = c("#D9D5CB"); borderHi = c("#B5B0A4"); text = c("#1F1E1B");
    muted = c("#66645D"); faint = c("#98958C"); accent = c("#C96442"); accentHi = c("#D97757");
    accentBg = c("#F7E4DC"); danger = c("#C4453A"); green = c("#3B8F4B"); warn = c("#B86A2E");
    chip = c("#F3F1EB"); chipBorder = c("#CCC8BD"); field = c("#FFFFFF"); panel = c("#FAF9F5");
    segment = c("#E6E3DB"); segOn = c("#FFFFFF"); line = c("#DEDAD1"); hover = c("#E2DFD6");
    onAccent = c("#FFFFFF"); warnBg = c("#FBEAE2"); warnBorder = c("#E7B9A5"); warnText = c("#8A3B22");
  }
}

static QString styleSheet() {
  const QMap<QString, QColor> tokens = {
      {"bg", bg}, {"sidebar", sidebar}, {"card", card}, {"cardHover", cardHover}, {"border", border},
      {"borderHi", borderHi}, {"text", text}, {"muted", muted}, {"faint", faint}, {"accent", accent},
      {"accentHi", accentHi}, {"accentBg", accentBg}, {"danger", danger}, {"field", field}, {"panel", panel},
      {"segment", segment}, {"segOn", segOn}, {"line", line}, {"hover", hover}, {"onAccent", onAccent},
      {"warnBg", warnBg}, {"warnBorder", warnBorder}, {"warnText", warnText}};
  QString s = QString::fromLatin1(kStyle);
  for (auto it = tokens.begin(); it != tokens.end(); ++it) s.replace('@' + it.key() + '@', it.value().name());
  return s;
}

static bool systemIsDark() {
  return QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light;
}

void apply(QApplication& app, const QString& mode) {
  static bool hooked = false;
  if (!hooked) {
    hooked = true;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont f = app.font();
    if (f.pointSizeF() > 0 && f.pointSizeF() < 10) f.setPointSizeF(10);
    app.setFont(f);
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
      if (g_mode == QLatin1String("system")) apply(app, g_mode);
    });
  }
  g_mode = mode;
  g_dark = mode == QLatin1String("dark") || (mode != QLatin1String("light") && systemIsDark());
  setPalette(g_dark);

  QPalette p;
  p.setColor(QPalette::Window, bg);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, field);
  p.setColor(QPalette::AlternateBase, card);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, card);
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, onAccent);
  p.setColor(QPalette::ToolTipBase, card);
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::PlaceholderText, faint);
  p.setColor(QPalette::Link, accent);
  p.setColor(QPalette::Mid, border);
  p.setColor(QPalette::Dark, lip);
  p.setColor(QPalette::Disabled, QPalette::Text, faint);
  p.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
  p.setColor(QPalette::Disabled, QPalette::WindowText, faint);
  app.setPalette(p);
  app.setStyleSheet(styleSheet());

  for (QWidget* w : QApplication::allWidgets()) w->update();
  if (g_onChanged) g_onChanged();
}

bool isDark() { return g_dark; }
void setOnChanged(std::function<void()> fn) { g_onChanged = std::move(fn); }

QIcon appIcon() {
  QIcon icon;
  for (int s : {16, 24, 32, 48, 64, 128, 256}) {
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x2B, 0x2A, 0x27));
    p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.22, s * 0.22);
    const qreal pad = s * 0.17, gap = s * 0.06;
    const qreal cell = (s - 2 * pad - 2 * gap) / 3.0;
    for (int i = 0; i < 9; i++) {
      const int r = i / 3, c = i % 3;
      p.setBrush(i == 4 ? QColor(0xD9, 0x77, 0x57) : (i % 2 == 0 ? QColor(0x6A, 0x67, 0x60) : QColor(0x4A, 0x48, 0x43)));
      p.drawRoundedRect(QRectF(pad + c * (cell + gap), pad + r * (cell + gap), cell, cell), cell * 0.26,
                        cell * 0.26);
    }
    p.end();
    icon.addPixmap(pm);
  }
  return icon;
}

void repolish(QWidget* w) {
  w->style()->unpolish(w);
  w->style()->polish(w);
  w->update();
}

}  // namespace theme
