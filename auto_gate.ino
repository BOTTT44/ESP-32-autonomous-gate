#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Hardware Pin Definitions
#define SERVO_PIN      27
#define BUZZER_PIN     12
#define TRIG_PIN       14
#define ECHO_PIN       26
#define PIR_PIN        13
#define POT_PIN        34 // ADC Pin for Potentiometer
#define BTN_ESTOP_PIN  16 // Button 1: GPIO 16 (E-Stop / Manual Mode)
#define BTN_RESET_PIN  17 // Button 2: GPIO 17 (Reset Auto / Manual Close)

// Hardware Objects
Servo gateServo;

// System States
enum OperatingMode { MODE_AUTOMATED, MODE_MANUAL };
OperatingMode currentMode = MODE_AUTOMATED;

enum AutoGateState { IDLE_CLOSED, OPENING, HOLDING_OPEN, CLOSING, SAFETY_REVERT };
AutoGateState autoState = IDLE_CLOSED;

int currentAngle = 0;
int lastPotAngle = -1; // Tracks physical dial movement
bool manualKnobOverride = true; // True = Potentiometer controls gate, False = Terminal fixed angle
bool isEStop = false; // Tracks if Manual Mode was triggered by Emergency Stop

// Button Debounce Timers
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 200;

int readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 25000); // 25ms timeout
  if (duration == 0) return 400; // Default out-of-range value
  return duration * 0.034 / 2;
}

void playEStopBeeps() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 1000, 350);
    delay(200);
  }
}

void updateOLED(const char* modeText, const char* statusText, int dist, bool motion, int potVal, int angle) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Header Bar
  display.setCursor(0, 0);
  display.print(F("MODE: "));
  display.println(modeText);

  // Status Output
  display.setCursor(0, 14);
  display.print(F("Status: "));
  display.println(statusText);

  // Sensor Readouts
  display.setCursor(0, 26);
  if (currentMode == MODE_AUTOMATED) {
    display.print(F("Dist: "));
    display.print(dist);
    display.print(F("cm | PIR: "));
    display.println(motion ? "ALERT" : "OK");
  } else {
    display.println(F("Sensors Override: OFF"));
  }

  // Potentiometer / Hold Delay Output
  display.setCursor(0, 38);
  if (currentMode == MODE_AUTOMATED) {
    int closeDelay = map(potVal, 0, 4095, 10, 1);
    display.print(F("Hold Delay: "));
    display.print(closeDelay);
    display.println(F(" sec"));
  } else {
    display.print(F("Gate Angle: "));
    display.print(angle);
    display.print((char)247); // Degree symbol
  }

  // Gate Position Gauge
  display.drawRect(0, 50, 128, 14, SSD1306_WHITE);
  display.fillRect(2, 52, map(angle, 0, 90, 0, 124), 10, SSD1306_WHITE);

  display.display();
}

void checkSerialInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    cmd = toupper(cmd); // Normalize input to uppercase

    // Ignore whitespace/newlines
    if (cmd == '\r' || cmd == '\n' || cmd == ' ') return;

    if (cmd == 'M') {
      if (currentMode == MODE_AUTOMATED) {
        currentMode = MODE_MANUAL;
        isEStop = false; // Standard manual transition
        manualKnobOverride = true; // Enable dial control
        noTone(BUZZER_PIN);
        Serial.println(F("--> [UART] Switched to MANUAL MODE"));
      }
    } else if (cmd == 'E') {
      currentMode = MODE_MANUAL;
      isEStop = true; // Emergency stop mode flag
      manualKnobOverride = true;
      noTone(BUZZER_PIN);
      playEStopBeeps();
      Serial.println(F("--> [UART] EMERGENCY STOP ACTIVATED -> MANUAL MODE"));
    } else if (cmd == 'A') {
      if (currentMode == MODE_MANUAL) {
        currentMode = MODE_AUTOMATED;
        isEStop = false;
        if (currentAngle > 0) {
          autoState = HOLDING_OPEN;
        } else {
          autoState = IDLE_CLOSED;
        }
        tone(BUZZER_PIN, 1200, 200);
        Serial.println(F("--> [UART] Switched to AUTOMATED MODE (Position Preserved)"));
      }
    } else if (cmd == 'O') {
      currentMode = MODE_MANUAL;
      manualKnobOverride = false; // Lock position to terminal command
      noTone(BUZZER_PIN);
      currentAngle = 90;
      gateServo.write(currentAngle);
      Serial.println(F("--> [UART] FULL OPEN (90 deg) -> Locked in MANUAL MODE"));
    } else if (cmd == 'C') {
      currentMode = MODE_MANUAL;
      manualKnobOverride = false; // Lock position to terminal command
      noTone(BUZZER_PIN);
      currentAngle = 0;
      gateServo.write(currentAngle);
      Serial.println(F("--> [UART] FULL CLOSE (0 deg) -> Locked in MANUAL MODE"));
    }
  }
}

void checkButtons() {
  unsigned long now = millis();

  // Button 1 (GPIO 16): E-STOP / Manual Toggle
  if (digitalRead(BTN_ESTOP_PIN) == LOW && (now - lastDebounceTime1 > debounceDelay)) {
    lastDebounceTime1 = now;
    if (currentMode == MODE_AUTOMATED) {
      currentMode = MODE_MANUAL;
      isEStop = true; // Button 1 explicitly triggers E-Stop state
      manualKnobOverride = true;
      noTone(BUZZER_PIN);
      playEStopBeeps();
      Serial.println(F("--> Switched to MANUAL / E-STOP MODE"));
    }
  }

  // Button 2 (GPIO 17): Reset to Automated Mode / Early Close
  if (digitalRead(BTN_RESET_PIN) == LOW && (now - lastDebounceTime2 > debounceDelay)) {
    lastDebounceTime2 = now;
    if (currentMode == MODE_MANUAL) {
      currentMode = MODE_AUTOMATED;
      isEStop = false;
      
      // Dynamic handoff: preserve position and choose state based on angle
      if (currentAngle > 0) {
        autoState = HOLDING_OPEN; // Gate is open or partially open; start holding timer
      } else {
        autoState = IDLE_CLOSED;  // Gate is at 0 degrees; enter idle closed state
      }

      tone(BUZZER_PIN, 1200, 200);
      Serial.println(F("--> Switched to AUTOMATED MODE (Position Preserved)"));
    } else if (currentMode == MODE_AUTOMATED && autoState == HOLDING_OPEN) {
      autoState = CLOSING; // Force gate to close early if already in auto mode
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Pin Modes
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(POT_PIN, INPUT);

  // Internal Pullups for Pushbuttons (GPIO 16 & GPIO 17)
  pinMode(BTN_ESTOP_PIN, INPUT_PULLUP);
  pinMode(BTN_RESET_PIN, INPUT_PULLUP);

  // Servo Setup
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  gateServo.setPeriodHertz(50);
  gateServo.attach(SERVO_PIN, 500, 2400);
  gateServo.write(0);

  // OLED Initialization
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED Failed!"));
    for (;;);
  }

  updateOLED("AUTOMATED", "SYSTEM READY", 0, false, analogRead(POT_PIN), 0);
  delay(1000);
}

void loop() {
  checkButtons();
  checkSerialInput();

  int potVal = analogRead(POT_PIN);

  // MODE 1: MANUAL / E-STOP OVERRIDE
  if (currentMode == MODE_MANUAL) {
    int targetAngle = map(potVal, 0, 4095, 0, 90);

    // Re-enable physical dial control if knob is turned more than 3 degrees
    if (lastPotAngle != -1 && abs(targetAngle - lastPotAngle) > 3) {
      manualKnobOverride = true;
    }
    lastPotAngle = targetAngle;

    // Apply potentiometer position ONLY if knob control is active
    if (manualKnobOverride) {
      currentAngle = targetAngle;
      gateServo.write(currentAngle);
    }

    // Determine status labels based on whether E-Stop or Terminal Manual was activated
    const char* headerText = isEStop ? "MANUAL / E-STOP" : "MANUAL";
    const char* statusText;

    if (isEStop) {
      statusText = manualKnobOverride ? "EMERGENCY STOP" : "UART SERVO LOCK";
    } else {
      statusText = manualKnobOverride ? "KNOB CONTROL" : "UART SERVO LOCK";
    }

    updateOLED(headerText, statusText, 0, false, potVal, currentAngle);
    delay(30);
    return;
  }

  // MODE 2: AUTOMATED STATE MACHINE
  int distance = readDistance();
  bool motion = digitalRead(PIR_PIN) == HIGH;

  int holdTimeMs = map(potVal, 0, 4095, 10000, 1000);
  int stepDelay = map(potVal, 0, 4095, 50, 10);

  switch (autoState) {
    case IDLE_CLOSED:
      gateServo.write(0);
      currentAngle = 0;
      updateOLED("AUTOMATED", "IDLE / CLOSED", distance, motion, potVal, 0);

      if (motion) {
        tone(BUZZER_PIN, 1800, 40);
      }

      if (distance < 15 && distance > 0) {
        autoState = OPENING;
      }
      break;

    case OPENING:
      tone(BUZZER_PIN, 1000);
      for (; currentAngle <= 90; currentAngle += 5) {
        checkButtons();
        checkSerialInput();
        if (currentMode == MODE_MANUAL) { noTone(BUZZER_PIN); return; }

        gateServo.write(currentAngle);
        updateOLED("AUTOMATED", "OPENING...", readDistance(), digitalRead(PIR_PIN), analogRead(POT_PIN), currentAngle);
        delay(stepDelay);
      }
      noTone(BUZZER_PIN);
      autoState = HOLDING_OPEN;
      break;

    case HOLDING_OPEN: {
      unsigned long startTime = millis();
      while (millis() - startTime < (unsigned long)holdTimeMs) {
        checkButtons();
        checkSerialInput();
        if (currentMode == MODE_MANUAL) { noTone(BUZZER_PIN); return; }
        if (autoState == CLOSING) break;

        potVal = analogRead(POT_PIN);
        holdTimeMs = map(potVal, 0, 4095, 10000, 1000);

        int currentDist = readDistance();
        updateOLED("AUTOMATED", "HOLDING OPEN", currentDist, digitalRead(PIR_PIN), potVal, currentAngle);

        if (currentDist < 15 && currentDist > 0) {
          startTime = millis(); // Refresh timer if object remains nearby
        }
        delay(80);
      }
      autoState = CLOSING;
      break;
    }

    case CLOSING:
      tone(BUZZER_PIN, 700);
      for (; currentAngle >= 0; currentAngle -= 5) {
        checkButtons();
        checkSerialInput();
        if (currentMode == MODE_MANUAL) { noTone(BUZZER_PIN); return; }

        int currentDist = readDistance();
        if (currentDist < 10 && currentDist > 0) {
          noTone(BUZZER_PIN);
          autoState = SAFETY_REVERT;
          break;
        }

        gateServo.write(currentAngle);
        updateOLED("AUTOMATED", "CLOSING...", currentDist, digitalRead(PIR_PIN), analogRead(POT_PIN), currentAngle);
        delay(stepDelay);
      }
      if (autoState != SAFETY_REVERT && currentMode == MODE_AUTOMATED) {
        noTone(BUZZER_PIN);
        autoState = IDLE_CLOSED;
      }
      break;

    case SAFETY_REVERT:
      tone(BUZZER_PIN, 2400);
      gateServo.write(90);
      currentAngle = 90;

      for (int i = 0; i < 4; i++) {
        checkButtons();
        checkSerialInput();
        if (currentMode == MODE_MANUAL) { noTone(BUZZER_PIN); return; }

        updateOLED("AUTOMATED", "! SAFETY REVERT !", readDistance(), true, analogRead(POT_PIN), 90);
        delay(150);
      }
      noTone(BUZZER_PIN);
      autoState = HOLDING_OPEN;
      break;
  }

  delay(30);
}