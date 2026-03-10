#include <Wire.h>
#include <Adafruit_MCP23X17.h>

#define MAX_MCP 8
#define TOTAL_PINS 64

// -------- MIDI settings --------
const uint8_t MIDI_CHANNEL = 1;     // 1..16
const uint8_t NOTE_BASE = 36;       // button 0 = MIDI note 36

// -------- debounce settings --------
const unsigned long DEBOUNCE_MS = 20;

// -------- MCP storage --------
Adafruit_MCP23X17 mcp[MAX_MCP];
uint8_t mcpAddress[MAX_MCP];
uint8_t mcpCount = 0;

// -------- button state --------
bool rawState[TOTAL_PINS];
bool stableState[TOTAL_PINS];
bool lastStableState[TOTAL_PINS];
unsigned long lastChangeTime[TOTAL_PINS];

// --------------------------------------------------
// MIDI send helpers
// --------------------------------------------------
void midiSendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
  Serial1.write(0x90 | ((channel - 1) & 0x0F));
  Serial1.write(note & 0x7F);
  Serial1.write(velocity & 0x7F);
}

void midiSendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
  Serial1.write(0x80 | ((channel - 1) & 0x0F));
  Serial1.write(note & 0x7F);
  Serial1.write(velocity & 0x7F);
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
    lastStableState[i] = state;
    lastChangeTime[i] = 0;
  }

  // mark non-existing pins as released
  for (uint8_t i = availablePins; i < TOTAL_PINS; i++) {
    rawState[i] = HIGH;
    stableState[i] = HIGH;
    lastStableState[i] = HIGH;
    lastChangeTime[i] = 0;
  }
}

// --------------------------------------------------
// Scan + debounce + MIDI
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

        uint8_t note = NOTE_BASE + i;

        // active low button
        if (stableState[i] == LOW) {
          midiSendNoteOn(note, 127, MIDI_CHANNEL);
          Serial.print("Button ");
          Serial.print(i);
          Serial.print(" pressed -> Note ON ");
          Serial.println(note);
        } else {
          midiSendNoteOff(note, 0, MIDI_CHANNEL);
          Serial.print("Button ");
          Serial.print(i);
          Serial.print(" released -> Note OFF ");
          Serial.println(note);
        }

        lastStableState[i] = stableState[i];
      }
    }
  }
}

void setup() {
  Serial.begin(115200);   // debug to Serial Monitor
  Serial1.begin(31250);   // MIDI OUT on TX1 (pin 18)

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

  Serial.println("Ready.");
}

void loop() {
  scanButtons();
}
