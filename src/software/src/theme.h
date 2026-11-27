#pragma once

#include <QColor>
#include <QString>
#include <functional>

class QApplication;
class QIcon;
class QWidget;

namespace theme {

// Current palette. Filled in by apply(); custom-painted widgets read these directly.
inline QColor bg, sidebar, card, cardHover, lip, border, borderHi, text, muted, faint, accent, accentHi, accentBg,
    danger, green, warn, chip, chipBorder, field, panel, segment, segOn, line, hover, onAccent, warnBg, warnBorder,
    warnText;

// mode: "system" (follows the OS light/dark setting, live), "dark" or "light"
void apply(QApplication& app, const QString& mode);
bool isDark();
void setOnChanged(std::function<void()> fn);  // called after every palette change

QIcon appIcon();
void repolish(QWidget* w);

}  // namespace theme
