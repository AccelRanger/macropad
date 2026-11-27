#include "keys.h"

#include <QHash>
#include <QKeyEvent>

#if defined(Q_OS_WIN)
#define SUPER "Win"
#elif defined(Q_OS_MACOS)
#define SUPER "Cmd"
#else
#define SUPER "Super"
#endif

// id, label, vk, vkExt, evdev, Qt::Key, modifier
static const std::vector<KeyDef> kKeys = {
    // modifiers
    {"Ctrl", "Ctrl", 0xA2, false, 29, Qt::Key_Control, true},
    {"Shift", "Shift", 0xA0, false, 42, Qt::Key_Shift, true},
    {"Alt", "Alt", 0xA4, false, 56, Qt::Key_Alt, true},
    {"Super", SUPER, 0x5B, true, 125, Qt::Key_Meta, true},
    {"RightCtrl", "Right Ctrl", 0xA3, true, 97, 0, true},
    {"RightShift", "Right Shift", 0xA1, false, 54, 0, true},
    {"RightAlt", "AltGr", 0xA5, true, 100, Qt::Key_AltGr, true},
    {"RightSuper", "Right " SUPER, 0x5C, true, 126, 0, true},

    // letters
    {"A", "A", 0x41, false, 30, Qt::Key_A, false},
    {"B", "B", 0x42, false, 48, Qt::Key_B, false},
    {"C", "C", 0x43, false, 46, Qt::Key_C, false},
    {"D", "D", 0x44, false, 32, Qt::Key_D, false},
    {"E", "E", 0x45, false, 18, Qt::Key_E, false},
    {"F", "F", 0x46, false, 33, Qt::Key_F, false},
    {"G", "G", 0x47, false, 34, Qt::Key_G, false},
    {"H", "H", 0x48, false, 35, Qt::Key_H, false},
    {"I", "I", 0x49, false, 23, Qt::Key_I, false},
    {"J", "J", 0x4A, false, 36, Qt::Key_J, false},
    {"K", "K", 0x4B, false, 37, Qt::Key_K, false},
    {"L", "L", 0x4C, false, 38, Qt::Key_L, false},
    {"M", "M", 0x4D, false, 50, Qt::Key_M, false},
    {"N", "N", 0x4E, false, 49, Qt::Key_N, false},
    {"O", "O", 0x4F, false, 24, Qt::Key_O, false},
    {"P", "P", 0x50, false, 25, Qt::Key_P, false},
    {"Q", "Q", 0x51, false, 16, Qt::Key_Q, false},
    {"R", "R", 0x52, false, 19, Qt::Key_R, false},
    {"S", "S", 0x53, false, 31, Qt::Key_S, false},
    {"T", "T", 0x54, false, 20, Qt::Key_T, false},
    {"U", "U", 0x55, false, 22, Qt::Key_U, false},
    {"V", "V", 0x56, false, 47, Qt::Key_V, false},
    {"W", "W", 0x57, false, 17, Qt::Key_W, false},
    {"X", "X", 0x58, false, 45, Qt::Key_X, false},
    {"Y", "Y", 0x59, false, 21, Qt::Key_Y, false},
    {"Z", "Z", 0x5A, false, 44, Qt::Key_Z, false},

    // digits
    {"1", "1", 0x31, false, 2, Qt::Key_1, false},
    {"2", "2", 0x32, false, 3, Qt::Key_2, false},
    {"3", "3", 0x33, false, 4, Qt::Key_3, false},
    {"4", "4", 0x34, false, 5, Qt::Key_4, false},
    {"5", "5", 0x35, false, 6, Qt::Key_5, false},
    {"6", "6", 0x36, false, 7, Qt::Key_6, false},
    {"7", "7", 0x37, false, 8, Qt::Key_7, false},
    {"8", "8", 0x38, false, 9, Qt::Key_8, false},
    {"9", "9", 0x39, false, 10, Qt::Key_9, false},
    {"0", "0", 0x30, false, 11, Qt::Key_0, false},

    // function keys
    {"F1", "F1", 0x70, false, 59, Qt::Key_F1, false},
    {"F2", "F2", 0x71, false, 60, Qt::Key_F2, false},
    {"F3", "F3", 0x72, false, 61, Qt::Key_F3, false},
    {"F4", "F4", 0x73, false, 62, Qt::Key_F4, false},
    {"F5", "F5", 0x74, false, 63, Qt::Key_F5, false},
    {"F6", "F6", 0x75, false, 64, Qt::Key_F6, false},
    {"F7", "F7", 0x76, false, 65, Qt::Key_F7, false},
    {"F8", "F8", 0x77, false, 66, Qt::Key_F8, false},
    {"F9", "F9", 0x78, false, 67, Qt::Key_F9, false},
    {"F10", "F10", 0x79, false, 68, Qt::Key_F10, false},
    {"F11", "F11", 0x7A, false, 87, Qt::Key_F11, false},
    {"F12", "F12", 0x7B, false, 88, Qt::Key_F12, false},
    {"F13", "F13", 0x7C, false, 183, Qt::Key_F13, false},
    {"F14", "F14", 0x7D, false, 184, Qt::Key_F14, false},
    {"F15", "F15", 0x7E, false, 185, Qt::Key_F15, false},
    {"F16", "F16", 0x7F, false, 186, Qt::Key_F16, false},
    {"F17", "F17", 0x80, false, 187, Qt::Key_F17, false},
    {"F18", "F18", 0x81, false, 188, Qt::Key_F18, false},
    {"F19", "F19", 0x82, false, 189, Qt::Key_F19, false},
    {"F20", "F20", 0x83, false, 190, Qt::Key_F20, false},
    {"F21", "F21", 0x84, false, 191, Qt::Key_F21, false},
    {"F22", "F22", 0x85, false, 192, Qt::Key_F22, false},
    {"F23", "F23", 0x86, false, 193, Qt::Key_F23, false},
    {"F24", "F24", 0x87, false, 194, Qt::Key_F24, false},

    // editing / navigation
    {"Esc", "Esc", 0x1B, false, 1, Qt::Key_Escape, false},
    {"Tab", "Tab", 0x09, false, 15, Qt::Key_Tab, false},
    {"CapsLock", "Caps Lock", 0x14, false, 58, Qt::Key_CapsLock, false},
    {"Space", "Space", 0x20, false, 57, Qt::Key_Space, false},
    {"Enter", "Enter", 0x0D, false, 28, Qt::Key_Return, false},
    {"Backspace", "Backspace", 0x08, false, 14, Qt::Key_Backspace, false},
    {"Insert", "Insert", 0x2D, true, 110, Qt::Key_Insert, false},
    {"Delete", "Delete", 0x2E, true, 111, Qt::Key_Delete, false},
    {"Home", "Home", 0x24, true, 102, Qt::Key_Home, false},
    {"End", "End", 0x23, true, 107, Qt::Key_End, false},
    {"PageUp", "Page Up", 0x21, true, 104, Qt::Key_PageUp, false},
    {"PageDown", "Page Down", 0x22, true, 109, Qt::Key_PageDown, false},
    {"Left", "←", 0x25, true, 105, Qt::Key_Left, false},
    {"Up", "↑", 0x26, true, 103, Qt::Key_Up, false},
    {"Right", "→", 0x27, true, 106, Qt::Key_Right, false},
    {"Down", "↓", 0x28, true, 108, Qt::Key_Down, false},
    {"PrintScreen", "Print Screen", 0x2C, true, 99, Qt::Key_Print, false},
    {"ScrollLock", "Scroll Lock", 0x91, false, 70, Qt::Key_ScrollLock, false},
    {"Pause", "Pause", 0x13, false, 119, Qt::Key_Pause, false},
    {"Menu", "Menu", 0x5D, true, 127, Qt::Key_Menu, false},

    // punctuation (US layout positions)
    {"Minus", "-", 0xBD, false, 12, Qt::Key_Minus, false},
    {"Equal", "=", 0xBB, false, 13, Qt::Key_Equal, false},
    {"LeftBracket", "[", 0xDB, false, 26, Qt::Key_BracketLeft, false},
    {"RightBracket", "]", 0xDD, false, 27, Qt::Key_BracketRight, false},
    {"Backslash", "\\", 0xDC, false, 43, Qt::Key_Backslash, false},
    {"Semicolon", ";", 0xBA, false, 39, Qt::Key_Semicolon, false},
    {"Apostrophe", "'", 0xDE, false, 40, Qt::Key_Apostrophe, false},
    {"Grave", "`", 0xC0, false, 41, Qt::Key_QuoteLeft, false},
    {"Comma", ",", 0xBC, false, 51, Qt::Key_Comma, false},
    {"Period", ".", 0xBE, false, 52, Qt::Key_Period, false},
    {"Slash", "/", 0xBF, false, 53, Qt::Key_Slash, false},
    {"IntlBackslash", "<>", 0xE2, false, 86, 0, false},

    // numpad
    {"NumLock", "Num Lock", 0x90, true, 69, Qt::Key_NumLock, false},
    {"Num0", "Num 0", 0x60, false, 82, 0, false},
    {"Num1", "Num 1", 0x61, false, 79, 0, false},
    {"Num2", "Num 2", 0x62, false, 80, 0, false},
    {"Num3", "Num 3", 0x63, false, 81, 0, false},
    {"Num4", "Num 4", 0x64, false, 75, 0, false},
    {"Num5", "Num 5", 0x65, false, 76, 0, false},
    {"Num6", "Num 6", 0x66, false, 77, 0, false},
    {"Num7", "Num 7", 0x67, false, 71, 0, false},
    {"Num8", "Num 8", 0x68, false, 72, 0, false},
    {"Num9", "Num 9", 0x69, false, 73, 0, false},
    {"NumMultiply", "Num *", 0x6A, false, 55, Qt::Key_Asterisk, false},
    {"NumAdd", "Num +", 0x6B, false, 78, Qt::Key_Plus, false},
    {"NumSubtract", "Num -", 0x6D, false, 74, 0, false},
    {"NumDecimal", "Num .", 0x6E, false, 83, 0, false},
    {"NumDivide", "Num /", 0x6F, true, 98, 0, false},
    {"NumEnter", "Num Enter", 0x0D, true, 96, Qt::Key_Enter, false},

    // media
    {"PlayPause", "Play/Pause", 0xB3, true, 164, Qt::Key_MediaTogglePlayPause, false},
    {"NextTrack", "Next Track", 0xB0, true, 163, Qt::Key_MediaNext, false},
    {"PrevTrack", "Prev Track", 0xB1, true, 165, Qt::Key_MediaPrevious, false},
    {"MediaStop", "Stop", 0xB2, true, 166, Qt::Key_MediaStop, false},
    {"VolumeUp", "Volume Up", 0xAF, true, 115, Qt::Key_VolumeUp, false},
    {"VolumeDown", "Volume Down", 0xAE, true, 114, Qt::Key_VolumeDown, false},
    {"Mute", "Mute", 0xAD, true, 113, Qt::Key_VolumeMute, false},
};

const std::vector<KeyDef>& allKeys() { return kKeys; }

namespace {
struct Index {
  QHash<QString, const KeyDef*> byId;
  QHash<int, const KeyDef*> byEvdev, byVk, byQt;
  Index() {
    for (const KeyDef& k : kKeys) {
      byId.insert(QString::fromLatin1(k.id).toLower(), &k);
      if (k.evdev) byEvdev.insert(k.evdev, &k);
      if (k.vk && !byVk.contains(k.vk)) byVk.insert(k.vk, &k);  // Enter wins over NumEnter
      if (k.qt) byQt.insert(k.qt, &k);
    }
  }
};
const Index& idx() {
  static const Index i;
  return i;
}
}  // namespace

const KeyDef* keyById(const QString& id) { return idx().byId.value(id.toLower(), nullptr); }
const KeyDef* keyByEvdev(int code) { return idx().byEvdev.value(code, nullptr); }
const KeyDef* keyByQt(int qtKey) { return idx().byQt.value(qtKey, nullptr); }

const KeyDef* keyByVk(int vk, int scanCode) {
  const bool ext = (scanCode & 0x100) != 0;
  const int sc = scanCode & 0xFF;
  switch (vk) {
    case 0x10: return keyById(sc == 0x36 ? "RightShift" : "Shift");  // generic VK_SHIFT
    case 0x11: return keyById(ext ? "RightCtrl" : "Ctrl");           // generic VK_CONTROL
    case 0x12: return keyById(ext ? "RightAlt" : "Alt");             // generic VK_MENU
    case 0x0D: return keyById(ext ? "NumEnter" : "Enter");
  }
  return idx().byVk.value(vk, nullptr);
}

const KeyDef* keyFromEvent(const QKeyEvent* e) {
#if defined(Q_OS_WIN)
  if (e->nativeVirtualKey())
    if (const KeyDef* k = keyByVk((int)e->nativeVirtualKey(), (int)e->nativeScanCode())) return k;
#elif defined(Q_OS_LINUX)
  // On both X11 and Wayland the native scan code is the XKB keycode = evdev code + 8.
  const quint32 sc = e->nativeScanCode();
  if (sc > 8)
    if (const KeyDef* k = keyByEvdev((int)sc - 8)) return k;
#endif
  return keyByQt(e->key());
}

const KeyDef* keyByName(const QString& name) {
  const QString n = name.trimmed();
  if (const KeyDef* k = keyById(n)) return k;
  static const QHash<QString, const char*> aliases = {
      {"control", "Ctrl"}, {"lctrl", "Ctrl"},  {"rctrl", "RightCtrl"}, {"win", "Super"},
      {"meta", "Super"},   {"cmd", "Super"},   {"windows", "Super"},    {"altgr", "RightAlt"},
      {"return", "Enter"}, {"escape", "Esc"},  {"del", "Delete"},       {"ins", "Insert"},
      {"pgup", "PageUp"},  {"pgdn", "PageDown"}, {"arrowleft", "Left"}, {"arrowright", "Right"},
      {"arrowup", "Up"},   {"arrowdown", "Down"}, {"print", "PrintScreen"}, {"prtsc", "PrintScreen"},
      {"play", "PlayPause"}, {"next", "NextTrack"}, {"prev", "PrevTrack"}, {"volup", "VolumeUp"},
      {"voldown", "VolumeDown"}, {"backtick", "Grave"}};
  if (const char* id = aliases.value(n.toLower(), nullptr)) return keyById(QLatin1String(id));
  for (const KeyDef& k : kKeys)
    if (n.compare(QString::fromUtf8(k.label), Qt::CaseInsensitive) == 0) return &k;
  return nullptr;
}

QString keyLabel(const QString& id) {
  const KeyDef* k = keyById(id);
  return k ? QString::fromUtf8(k->label) : id;
}

QString comboText(const QStringList& ids) {
  QStringList parts;
  for (const QString& id : ids) parts << keyLabel(id);
  return parts.join(QStringLiteral(" + "));
}

QStringList sortCombo(const QStringList& ids) {
  static const char* order[] = {"Ctrl", "RightCtrl", "Shift", "RightShift",
                                "Alt",  "RightAlt",  "Super", "RightSuper"};
  QStringList out;
  for (const char* m : order)
    for (const QString& id : ids)
      if (id.compare(QLatin1String(m), Qt::CaseInsensitive) == 0 && !out.contains(id)) out << id;
  for (const QString& id : ids)
    if (!out.contains(id)) out << id;
  return out;
}
