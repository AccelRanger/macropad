// Linux backend.
//  - serial:     termios on /dev/ttyACM* / /dev/ttyUSB*
//  - keystrokes: a virtual keyboard created through /dev/uinput. This works the
//                same on Wayland and X11, but needs write access to /dev/uinput
//                (see packaging/70-macropad.rules).
#include <fcntl.h>
#include <grp.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#include <cerrno>
#include <cstring>

#include "keys.h"
#include "platform.h"

namespace plat {

static QString readSys(const QString& p) {
  QFile f(p);
  return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

std::vector<PortInfo> listPorts() {
  std::vector<PortInfo> out;
  const QStringList names =
      QDir("/dev").entryList({"ttyACM*", "ttyUSB*"}, QDir::System | QDir::AllEntries);
  for (const QString& n : names) {
    PortInfo pi;
    pi.path = "/dev/" + n;
    // /sys/class/tty/<n>/device is the USB interface; its parent is the USB device.
    const QString usb = "/sys/class/tty/" + n + "/device/../";
    const QString vendor = readSys(usb + "idVendor");
    pi.description = (readSys(usb + "manufacturer") + ' ' + readSys(usb + "product")).trimmed();
    pi.likelyMacropad = vendor.compare("303a", Qt::CaseInsensitive) == 0;
    out.push_back(pi);
  }
  std::sort(out.begin(), out.end(), [](const PortInfo& a, const PortInfo& b) {
    if (a.path.size() != b.path.size()) return a.path.size() < b.path.size();
    return a.path < b.path;
  });
  return out;
}

static QString groupOf(const QString& path) {
  struct stat st;
  if (stat(QFile::encodeName(path).constData(), &st) != 0) return QString();
  const group* g = getgrgid(st.st_gid);
  return g ? QString::fromLocal8Bit(g->gr_name) : QString();
}

intptr_t portOpen(const QString& path, QString* err) {
  const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    const int e = errno;
    if (err) {
      if (e == EACCES) {
        const QString grp = groupOf(path);
        *err = QStringLiteral("No permission to open %1. Install the udev rule from packaging/ "
                              "or add yourself to the '%2' group, then log in again.")
                   .arg(path, grp.isEmpty() ? QStringLiteral("uucp") : grp);
      } else if (e == EBUSY) {
        *err = QStringLiteral("%1 is busy (is a serial monitor open?).").arg(path);
      } else {
        *err = QStringLiteral("Could not open %1: %2").arg(path, QString::fromLocal8Bit(strerror(e)));
      }
    }
    return -1;
  }
  if (ioctl(fd, TIOCEXCL) != 0 && errno == EBUSY) {
    ::close(fd);
    if (err) *err = QStringLiteral("%1 is busy (is a serial monitor open?).").arg(path);
    return -1;
  }

  termios t;
  if (tcgetattr(fd, &t) == 0) {
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~CRTSCTS;
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
  }
  // Same line state as the Windows build: DTR on (ESP32 USB-CDC only transmits
  // when DTR is set), RTS off (RTS without DTR resets the chip).
  int dtr = TIOCM_DTR, rts = TIOCM_RTS;
  ioctl(fd, TIOCMBIS, &dtr);
  ioctl(fd, TIOCMBIC, &rts);
  tcflush(fd, TCIOFLUSH);
  return fd;
}

int portRead(intptr_t h, char* buf, int n) {
  pollfd pfd = {(int)h, POLLIN, 0};
  const int r = poll(&pfd, 1, 100);
  if (r < 0) return errno == EINTR ? 0 : -1;
  if (r == 0) return 0;
  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
  const ssize_t k = ::read((int)h, buf, n);
  if (k > 0) return (int)k;
  if (k < 0 && (errno == EAGAIN || errno == EINTR)) return 0;
  return -1;  // EOF after POLLIN = device gone
}

void portClose(intptr_t h) {
  if (h >= 0) ::close((int)h);
}

// ---------------------------------------------------------------------------
// uinput
// ---------------------------------------------------------------------------
static int g_ui = -1;

bool injectInit(QString* err) {
  const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    if (err)
      *err = QStringLiteral("Can't send keystrokes: no access to /dev/uinput (%1). "
                            "Install packaging/70-macropad.rules and log in again. Commands still work.")
                 .arg(QString::fromLocal8Bit(strerror(errno)));
    return false;
  }
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_SYN);
  for (const KeyDef& k : allKeys())
    if (k.evdev) ioctl(fd, UI_SET_KEYBIT, k.evdev);

  uinput_setup us;
  memset(&us, 0, sizeof us);
  us.id.bustype = BUS_VIRTUAL;
  us.id.vendor = 0x303a;
  us.id.product = 0x4d50;
  snprintf(us.name, UINPUT_MAX_NAME_SIZE, "Macropad Studio virtual keyboard");
  if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
    if (err) *err = QStringLiteral("Can't create the virtual keyboard: %1").arg(QString::fromLocal8Bit(strerror(errno)));
    ::close(fd);
    return false;
  }
  g_ui = fd;
  return true;
}

void injectShutdown() {
  if (g_ui < 0) return;
  ioctl(g_ui, UI_DEV_DESTROY);
  ::close(g_ui);
  g_ui = -1;
}

static void emitEv(int type, int code, int value) {
  input_event ev;
  memset(&ev, 0, sizeof ev);
  ev.type = (__u16)type;
  ev.code = (__u16)code;
  ev.value = value;
  if (::write(g_ui, &ev, sizeof ev) < 0) { /* nothing sensible to do */ }
}

bool injectKeys(const std::vector<const KeyDef*>& keys, QString* err) {
  if (g_ui < 0) {
    if (err) *err = QStringLiteral("Keystrokes are unavailable (no /dev/uinput access).");
    return false;
  }
  for (const KeyDef* k : keys) {
    emitEv(EV_KEY, k->evdev, 1);
    emitEv(EV_SYN, SYN_REPORT, 0);
  }
  QThread::usleep(2000);  // give the compositor a frame to see the chord
  for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
    emitEv(EV_KEY, (*it)->evdev, 0);
    emitEv(EV_SYN, SYN_REPORT, 0);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Text typing.
// uinput only knows physical keys, so text is mapped through a US layout.
// When wtype is installed on Wayland we use it instead: it uploads its own
// keymap, so any character (accents, emoji) and any layout work.
// ---------------------------------------------------------------------------
static bool usWtype() {
  return !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
         !QStandardPaths::findExecutable(QStringLiteral("wtype")).isEmpty();
}

// char -> (evdev code, needs shift) on a US layout
static bool usKey(QChar ch, int* code, bool* shift) {
  static const char* row = "1234567890-=qwertyuiop[]asdfghjkl;'`\\zxcvbnm,./";
  static const char* shifted = "!@#$%^&*()_+QWERTYUIOP{}ASDFGHJKL:\"~|ZXCVBNM<>?";
  static const int codes[] = {2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 16, 17, 18, 19,
                              20, 21, 22, 23, 24, 25, 26, 27, 30, 31, 32, 33, 34, 35, 36, 37,
                              38, 39, 40, 41, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53};
  const ushort u = ch.unicode();
  if (u == ' ') { *code = 57; *shift = false; return true; }
  if (u == '\n') { *code = 28; *shift = false; return true; }
  if (u == '\t') { *code = 15; *shift = false; return true; }
  if (u == 0 || u > 127) return false;
  if (const char* p = strchr(row, (char)u)) { *code = codes[p - row]; *shift = false; return true; }
  if (const char* p = strchr(shifted, (char)u)) { *code = codes[p - shifted]; *shift = true; return true; }
  return false;
}

bool typeText(const QString& text, QString* err) {
  if (text.isEmpty()) return true;
  if (usWtype()) {
    QProcess p;
    QString t = text;
    t.remove('\r');
    p.start(QStringLiteral("wtype"), {QStringLiteral("--"), t});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
      if (err) *err = QStringLiteral("wtype failed: %1").arg(QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
      return false;
    }
    return true;
  }
  if (g_ui < 0) {
    if (err) *err = QStringLiteral("Typing is unavailable (no /dev/uinput access).");
    return false;
  }
  int skipped = 0;
  for (QChar ch : text) {
    int code;
    bool shift;
    if (ch == '\r') continue;
    if (!usKey(ch, &code, &shift)) { skipped++; continue; }
    if (shift) { emitEv(EV_KEY, 42, 1); emitEv(EV_SYN, SYN_REPORT, 0); }
    emitEv(EV_KEY, code, 1);
    emitEv(EV_SYN, SYN_REPORT, 0);
    emitEv(EV_KEY, code, 0);
    emitEv(EV_SYN, SYN_REPORT, 0);
    if (shift) { emitEv(EV_KEY, 42, 0); emitEv(EV_SYN, SYN_REPORT, 0); }
    QThread::usleep(1500);
  }
  if (skipped && err) {
    *err = QStringLiteral("%1 character(s) could not be typed. Install wtype for full Unicode.").arg(skipped);
    return false;
  }
  return true;
}

QString typingNote() {
  if (usWtype()) return QStringLiteral("Typed with wtype: any characters and keyboard layouts work.");
  return QStringLiteral("Typed as US-layout keys (ASCII only). Install “wtype” for accents, emoji and other layouts.");
}

// ---------------------------------------------------------------------------
bool runCommand(const QString& cmd, QString* err) {
  QProcess p;
  p.setProgram(QStringLiteral("/bin/sh"));
  p.setArguments({QStringLiteral("-c"), cmd});
  p.setWorkingDirectory(QDir::homePath());
  p.setStandardOutputFile(QProcess::nullDevice());
  p.setStandardErrorFile(QProcess::nullDevice());
  if (!p.startDetached()) {
    if (err) *err = QStringLiteral("Could not start /bin/sh.");
    return false;
  }
  return true;
}

QString shellName() { return QStringLiteral("/bin/sh"); }

QStringList commandExamples() {
  return {QStringLiteral("firefox"), QStringLiteral("xdg-open https://github.com"),
          QStringLiteral("playerctl play-pause"), QStringLiteral("~/scripts/deploy.sh")};
}

void decorateWindow(QWidget*) {}

}  // namespace plat
