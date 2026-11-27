# Macropad Studio

Desktop app for the 3×3 macropad. The pad sends the pressed key number (`1`–`9`) over USB serial. For each key, the app sends a keyboard shortcut or runs a command, depending on the active preset.

Runs on **Linux** (Wayland and X11) and **Windows**. Built with C++17 and Qt 6 Widgets (6.5 or newer).

## Features

- **Presets**: switch from the sidebar, or from the pad with a *preset key* (Settings ⚙): hold it and press
  another key to jump to preset 1–8, or tap it for the next preset.
- **Three triggers per key**: tap, hold and double-tap, each with its own action. Keys that only have a tap action
  still fire instantly on press.
- **Action types**
  - *Shortcut*: record any key combination. Media, volume, F13–F24 and other keys are under "Special keys".
  - *Type text*: types a snippet.
  - *Sequence*: a small script, one step per line:
    ```
    keys Ctrl+L        # press a shortcut
    wait 100           # ms, or 1.5s
    text github.com\n  # type text (\n = Enter, \t = Tab)
    run notify-send hi # run a command
    open ~/Downloads   # open a URL, file or folder
    ```
  - *Command*: runs through `/bin/sh` (Linux) or `cmd.exe` (Windows).
  - *Open*: a website, file or folder, opened with its default app.
- **Labels and icons** on each key: an emoji/text or an image (images are copied into the config folder).
- **Auto-reconnect**: finds the pad again after it's unplugged, even if the port name changes.
- **Themes**: light, dark, or match the system (switches live).

## Serial protocol

The firmware prints one line per event at 115200 baud:

| Line | Meaning |
|------|---------|
| `3`  | key 3 pressed |
| `r3` | key 3 released |

Hold and the hold-the-preset-key gesture need the release lines, so flash the current
`firmware/macropad.cpp`. With older firmware, tap and double-tap still work.

## Code layout

```
src/
  main.cpp            entry point
  mainwindow.*        window: sidebar (device + presets), key grid, editor, settings
  padengine.*         tap / hold / double-tap detection, preset key
  actions.*           runs actions, sequence script parser
  widgets.*           custom-painted keycap tiles + shortcut recorder
  theme.*             colours, stylesheet, app icon
  keys.*              platform-neutral key table (id ↔ Windows VK ↔ Linux evdev)
  config.*            JSON presets (+ import of the old presets.txt)
  serial.*            background reader thread
  platform.h          OS interface
  platform_linux.cpp  termios serial, uinput keystrokes, /bin/sh
  platform_win.cpp    Win32 serial, SendInput, cmd.exe
packaging/            udev rule, .desktop file, icon
```

## Build

### Linux

```sh
# Arch / CachyOS
sudo pacman -S --needed qt6-base cmake
# Debian / Ubuntu
sudo apt install qt6-base-dev cmake g++

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/macropad-studio
```

To build without CMake, run `./build-linux.sh`.

### Windows

Install Qt 6 (MinGW or MSVC kit) and CMake, then:

```bat
cmake -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\mingw_64
cmake --build build --config Release
windeployqt build\macropad-studio.exe
```

## Linux permissions

The app needs access to two device files:

| What | Why | Default access |
|------|-----|----------------|
| `/dev/uinput` | creates a virtual keyboard that sends the shortcuts | root only on most distros |
| `/dev/ttyACM*` | reads the macropad | `uucp` group (Arch) / `dialout` (Debian) |

The included udev rule grants both to the logged-in user:

```sh
sudo cp packaging/70-macropad.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

Then replug the pad. If shortcuts still don't work, log out and back in. Without uinput access, commands still run but shortcuts are disabled, and the app shows a warning in the sidebar.

**Typing text on Linux:** uinput sends physical keys, so *Type text* uses a US layout and handles ASCII only.
Install [`wtype`](https://github.com/atx/wtype) (`sudo pacman -S wtype`) and the app uses it automatically on
Wayland, so any characters and keyboard layouts work. On Windows, any text works out of the box.

## Config

- Linux: `~/.config/MacropadStudio/config.json`
- Windows: `%LOCALAPPDATA%\MacropadStudio\config.json`

On first start on Windows, presets from the old app (`%APPDATA%\MacropadStudio\presets.txt`) are imported automatically.
