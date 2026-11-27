cmake_minimum_required(VERSION 3.19)
project(MacropadStudio VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(SOURCES
  src/main.cpp
  src/mainwindow.cpp
  src/widgets.cpp
  src/theme.cpp
  src/keys.cpp
  src/config.cpp
  src/serial.cpp
  src/actions.cpp
  src/padengine.cpp
)
if(WIN32)
  list(APPEND SOURCES src/platform_win.cpp)
elseif(UNIX AND NOT APPLE)
  list(APPEND SOURCES src/platform_linux.cpp)
else()
  message(FATAL_ERROR "Macropad Studio supports Linux and Windows.")
endif()

qt_add_executable(macropad-studio ${SOURCES})
target_link_libraries(macropad-studio PRIVATE Qt6::Widgets)

if(WIN32)
  set_target_properties(macropad-studio PROPERTIES WIN32_EXECUTABLE ON)
  target_link_libraries(macropad-studio PRIVATE user32 advapi32 setupapi)
endif()

include(GNUInstallDirs)
install(TARGETS macropad-studio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
  install(FILES packaging/macropad-studio.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES packaging/macropad-studio.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()
