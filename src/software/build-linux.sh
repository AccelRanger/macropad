#!/bin/sh
# Quick build without CMake (needs Qt 6 dev files + pkg-config).
set -e
cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 -fPIC -Wall -Wextra \
  src/main.cpp src/mainwindow.cpp src/widgets.cpp src/theme.cpp src/keys.cpp \
  src/config.cpp src/serial.cpp src/actions.cpp src/padengine.cpp src/platform_linux.cpp \
  $(pkg-config --cflags --libs Qt6Widgets) -o build/macropad-studio
echo "built: build/macropad-studio"
