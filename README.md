# Macropad

Custom macropad with a custom PCB and firmware.

## How it works

The firmware running on the **ESP32-C6-1M** sends button-number data to the software running on the computer.

The computer-side software processes the received data and emulates keyboard input based on the presets configured on the computer.

## Hardware

- **Microcontroller:** ESP32-C6-1M
- **Button layout:** 3 rows × 3 columns
- **Total buttons:** 9
- **PCB:** Custom-designed PCB

  ![pcb](/src/img/main.png)

## Planned features

- Bash commands

## Signal flow

1. A button is pressed on the macropad.
2. The ESP32 firmware identifies the button and sends its number to the computer.
3. The computer-side software receives the signal.
4. The software checks the active preset for that button's binding.
5. The software sends the configured keyboard input.

## Project status

This document describes the intended design and features. Firmware and computer-side software is almost done.
