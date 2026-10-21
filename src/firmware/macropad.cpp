// -- Macropad.cpp -- //

#include <Arduino.h>

static const uint8_t ROWS = 3;
static const uint8_t COLS = 3;
static const uint8_t rowPins[ROWS] = {0, 1, 2};
static const uint8_t colPins[COLS] = {3, 4, 5};
static const uint32_t DEBOUNCE_MS = 50;
static const uint32_t SERIAL_BAUD = 115200;

//---------

static const uint8_t KEY_COUNT = ROWS * COLS;

static bool     stableState[KEY_COUNT];  // debounced state (true = pressed)
static bool     rawState[KEY_COUNT];
static uint32_t lastChange[KEY_COUNT];

static void scanMatrix(bool *out) {
  for (uint8_t r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], LOW);
    delayMicroseconds(10);
    for (uint8_t c = 0; c < COLS; c++) {
      out[r * COLS + c] = (digitalRead(colPins[c]) == LOW);
    }
    pinMode(rowPins[r], INPUT);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  for (uint8_t r = 0; r < ROWS; r++) pinMode(rowPins[r], INPUT);
  for (uint8_t c = 0; c < COLS; c++) pinMode(colPins[c], INPUT_PULLUP);

  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    stableState[i] = false;
    rawState[i] = false;
    lastChange[i] = 0;
  }
}

void loop() {
  bool now[KEY_COUNT];
  scanMatrix(now);
  uint32_t t = millis();

  for (uint8_t i = 0; i < KEY_COUNT; i++) {
    if (now[i] != rawState[i]) {
      rawState[i] = now[i];
      lastChange[i] = t;
    }
    if (rawState[i] != stableState[i] && (t - lastChange[i]) >= DEBOUNCE_MS) {
      stableState[i] = rawState[i];
      if (stableState[i]) {
        Serial.println(i + 1);
      }
    }
  }
  delay(1);
}