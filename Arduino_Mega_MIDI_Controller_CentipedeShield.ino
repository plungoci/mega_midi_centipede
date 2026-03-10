#include <Wire.h>
#include <Adafruit_MCP23X17.h>

#define MAX_MCP 8
#define TOTAL_PINS 64

// -------- MIDI settings --------
const uint8_t MIDI_CHANNEL = 1;        // 1..16
const uint8_t CC_BASE = 16;            // first CC number in bank 0, normal layer
const uint8_t BANK_COUNT = 4;          // number of CC banks
const uint8_t LAYER_CC_OFFSET = 64;    // shift layer CC offset (normal + 64)
const uint8_t CC_ON_VALUE = 127;       // value sent when toggle turns ON
const uint8_t CC_OFF_VALUE = 0;        // value sent when toggle turns OFF

// -------- debounce and hold settings --------
const unsigned long DEBOUNCE_MS = 20;
const unsigned long LONG_PRESS_MS = 550;

// -------- matrix layout --------
const uint8_t MATRIX_ROWS = 8;
const uint8_t MATRIX_COLS = 8;

// -------- special buttons in matrix --------
const uint8_t BANK_BUTTON_INDEX = 56;   // row 7, col 0
const uint8_t SHIFT_BUTTON_INDEX = 57;  // row 7, col 1

// -------- optional local LED feedback --------
const int8_t BANK_LED_PIN = -1;
const int8_t SHIFT_LED_PIN = LED_BUILTIN;

// -------- MCP storage --------
Adafruit_MCP23X17 mcp[MAX_MCP];
uint8_t mcpAddress[MAX_MCP];
uint8_t mcpCount = 0;

// -------- button state --------
bool rawState[TOTAL_PINS];
bool stableState[TOTAL_PINS];
unsigned long lastChangeTime[TOTAL_PINS];
unsigned long pressStartTime[TOTAL_PINS];
bool longPressHandled[TOTAL_PINS];

// -------- controller logic --------
bool toggleState[TOTAL_PINS];
bool ledState[TOTAL_PINS];
uint8_t activeBank = 0;
bool shiftLayerMomentary = false;
bool shiftLayerLatched = false;

// --------------------------------------------------
// MIDI send helpers (CC mode)
// --------------------------------------------------
void midiSendCC(uint8_t cc, uint8_t value, uint8_t channel) {
  Serial1.write(0xB0 | ((channel - 1) & 0x0F));
  Serial1.write(cc & 0x7F);
  Serial1.write(value & 0x7F);
}

// --------------------------------------------------
// Utility helpers
// --------------------------------------------------
bool isShiftLayerActive() {
  return shiftLayerMomentary || shiftLayerLatched;
}

uint8_t matrixToLinear(uint8_t row, uint8_t col) {
  return (row * MATRIX_COLS) + col;
}

uint8_t getCCNumberForButton(uint8_t buttonIndex) {
  uint8_t row = buttonIndex / MATRIX_COLS;
  uint8_t col = buttonIndex % MATRIX_COLS;

  // matrix-style performance layout with serpentine rows
  uint8_t matrixIndex;
  if ((row & 0x01) == 0) {
    matrixIndex = matrixToLinear(row, col);
  } else {
    matrixIndex = matrixToLinear(row, (MATRIX_COLS - 1) - col);
  }

  uint8_t cc = CC_BASE + matrixIndex + (activeBank * TOTAL_PINS);
  if (isShiftLayerActive()) {
    cc += LAYER_CC_OFFSET;
  }
  return cc & 0x7F;
}

void updateHardwareLedIndicators() {
  if (BANK_LED_PIN >= 0) {
    // pulse pattern by bank parity
    digitalWrite(BANK_LED_PIN, (activeBank & 0x01) ? HIGH : LOW);
  }
  if (SHIFT_LED_PIN >= 0) {
    digitalWrite(SHIFT_LED_PIN, isShiftLayerActive() ? HIGH : LOW);
  }
}

void setButtonLed(uint8_t buttonIndex, bool on) {
  ledState[buttonIndex] = on;
  // Hook for external LED hardware if present.
}

void refreshAllButtonLeds() {
  for (uint8_t i = 0; i < TOTAL_PINS; i++) {
    setButtonLed(i, toggleState[i]);
  }
  updateHardwareLedIndicators();
}

void handleBankSwitch() {
  activeBank = (activeBank + 1) % BANK_COUNT;
  Serial.print("Bank changed to ");
  Serial.println(activeBank);

  // refresh feedback for active bank/layer
  refreshAllButtonLeds();
}

void handleShiftLongPress() {
  shiftLayerLatched = !shiftLayerLatched;
  Serial.print("Shift layer latch: ");
  Serial.println(shiftLayerLatched ? "ON" : "OFF");
  refreshAllButtonLeds();
}

void handlePerformanceToggle(uint8_t buttonIndex) {
  toggleState[buttonIndex] = !toggleState[buttonIndex];
  setButtonLed(buttonIndex, toggleState[buttonIndex]);

  uint8_t cc = getCCNumberForButton(buttonIndex);
  uint8_t value = toggleState[buttonIndex] ? CC_ON_VALUE : CC_OFF_VALUE;
  midiSendCC(cc, value, MIDI_CHANNEL);

  Serial.print("Button ");
  Serial.print(buttonIndex);
  Serial.print(" toggled ");
  Serial.print(toggleState[buttonIndex] ? "ON" : "OFF");
  Serial.print(" -> CC ");
  Serial.print(cc);
  Serial.print(" value ");
  Serial.println(value);
}

// --------------------------------------------------
// I2C helpers
// --------------------------------------------------
bool i2cDeviceExists(uint8_t address) {
  Wire.beginTransmission(address);
  return (Wire.endTransmission() == 0);
}

void detectMCP23017() {
  mcpCount = 0;

  Serial.println("Scanning I2C addresses 0x20..0x27");

  for (uint8_t addr = 0x20; addr <= 0x27; addr++) {
    if (i2cDeviceExists(addr)) {
      Serial.print("Found I2C device at 0x");
      Serial.println(addr, HEX);

      if (mcpCount < MAX_MCP) {
        if (mcp[mcpCount].begin_I2C(addr)) {
          mcpAddress[mcpCount] = addr;
          Serial.print("Initialized MCP23017 at 0x");
          Serial.println(addr, HEX);
          mcpCount++;
        } else {
          Serial.print("Device at 0x");
          Serial.print(addr, HEX);
          Serial.println(" did not initialize as MCP23017");
        }
      }
    }
  }

  Serial.print("Total MCP23017 detected: ");
  Serial.println(mcpCount);
}

// --------------------------------------------------
// Pin mapping
// global pin 0..63 maps across detected chips
// chip = pin / 16
// local = pin % 16
// --------------------------------------------------
bool readButton(uint8_t globalPin) {
  uint8_t chip = globalPin / 16;
  uint8_t localPin = globalPin % 16;

  if (chip >= mcpCount) {
    return HIGH; // not present
  }

  return mcp[chip].digitalRead(localPin);
}

// --------------------------------------------------
// Setup MCP pins
// --------------------------------------------------
void setupInputs() {
  uint8_t availablePins = mcpCount * 16;
  if (availablePins > TOTAL_PINS) availablePins = TOTAL_PINS;

  for (uint8_t i = 0; i < availablePins; i++) {
    uint8_t chip = i / 16;
    uint8_t localPin = i % 16;

    mcp[chip].pinMode(localPin, INPUT);
    mcp[chip].pullUp(localPin, HIGH);

    bool state = mcp[chip].digitalRead(localPin);
    rawState[i] = state;
    stableState[i] = state;
    lastChangeTime[i] = 0;
    pressStartTime[i] = 0;
    longPressHandled[i] = false;
    toggleState[i] = false;
    ledState[i] = false;
  }

  // mark non-existing pins as released
  for (uint8_t i = availablePins; i < TOTAL_PINS; i++) {
    rawState[i] = HIGH;
    stableState[i] = HIGH;
    lastChangeTime[i] = 0;
    pressStartTime[i] = 0;
    longPressHandled[i] = false;
    toggleState[i] = false;
    ledState[i] = false;
  }
}

// --------------------------------------------------
// Scan + debounce + toggle/CC handling
// --------------------------------------------------
void scanButtons() {
  uint8_t availablePins = mcpCount * 16;
  if (availablePins > TOTAL_PINS) availablePins = TOTAL_PINS;

  unsigned long now = millis();

  for (uint8_t i = 0; i < availablePins; i++) {
    bool current = readButton(i);

    // raw edge detected
    if (current != rawState[i]) {
      rawState[i] = current;
      lastChangeTime[i] = now;
    }

    // debounce timeout passed
    if ((now - lastChangeTime[i]) >= DEBOUNCE_MS) {
      if (stableState[i] != rawState[i]) {
        stableState[i] = rawState[i];

        // active-low buttons
        if (stableState[i] == LOW) {
          pressStartTime[i] = now;
          longPressHandled[i] = false;

          if (i == SHIFT_BUTTON_INDEX) {
            shiftLayerMomentary = true;
            updateHardwareLedIndicators();
            Serial.println("Shift (momentary) ON");
          } else if (i == BANK_BUTTON_INDEX) {
            handleBankSwitch();
          } else {
            handlePerformanceToggle(i);
          }
        } else {
          // release
          if (i == SHIFT_BUTTON_INDEX) {
            shiftLayerMomentary = false;
            updateHardwareLedIndicators();
            Serial.println("Shift (momentary) OFF");
          }
          longPressHandled[i] = false;
          pressStartTime[i] = 0;
        }
      }
    }

    // long-press detection while held
    if ((stableState[i] == LOW) && !longPressHandled[i] && (pressStartTime[i] != 0)) {
      if ((now - pressStartTime[i]) >= LONG_PRESS_MS) {
        if (i == SHIFT_BUTTON_INDEX) {
          handleShiftLongPress();
        }
        longPressHandled[i] = true;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);   // debug to Serial Monitor
  Serial1.begin(31250);   // MIDI OUT on TX1 (pin 18)

  if (BANK_LED_PIN >= 0) {
    pinMode(BANK_LED_PIN, OUTPUT);
  }
  if (SHIFT_LED_PIN >= 0) {
    pinMode(SHIFT_LED_PIN, OUTPUT);
  }

  Wire.begin();           // Mega uses SDA=20, SCL=21
  delay(200);

  Serial.println();
  Serial.println("Centipede MIDI Controller starting...");

  detectMCP23017();

  if (mcpCount == 0) {
    Serial.println("No MCP23017 found.");
    Serial.println("Check shield, power, SDA/SCL, and addresses.");
    while (1);
  }

  setupInputs();
  refreshAllButtonLeds();

  Serial.println("Ready (Toggle + CC + Banks + Shift + Matrix layout).");
}

void loop() {
  scanButtons();
}
