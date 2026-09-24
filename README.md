# ESP-32-autonomous-gate
a gate that open and closes using ultrasonic and pir sensor


ESP32 Smart Barrier & Monitoring SystemA comprehensive microcontroller-based automation system using an ESP32 DevKit to handle motion detection, distance measurement, automated servo barrier gate actuation, local status display, and emergency safety overrides.

                  +-----------------------------------+
                  |           ESP32 (WROOM/U)          |
                  +-----------------------------------+
  Breadboard 3.3V | [3V3]                       [GND] | ---> Servo Motor GND
                  | [EN]                     [GPIO23] |
                  | [GPIO36]                 [GPIO22] | ---> OLED SCL
                  | [GPIO39]                  [TX0/1] |
 Potentiometer SIG | [GPIO34]                  [RX0/3] |
                  | [GPIO35]                 [GPIO21] | ---> OLED SDA
                  | [GPIO32]                    [GND] | ---> Breadboard GND Rail
                  | [GPIO33]                 [GPIO19] |
                  | [GPIO25]                 [GPIO18] |
  Ultrasonic ECHO | [GPIO26]                  [GPIO5] |
        Servo PWM | [GPIO27]                 [GPIO17] | ---> RESET Button (Pin 2.l)
  Ultrasonic TRIG | [GPIO14]                 [GPIO16] | ---> ESTOP Button (Pin 2.l)
 Piezo Buzzer (+2)| [GPIO12]                  [GPIO4] |
                  | [GND]                     [GPIO0] |
          PIR OUT | [GPIO13]                  [GPIO2] |
                  | [GPIO9]                  [GPIO15] |
                  | [GPIO10]                  [SD1/8] |
                  | [GPIO11]                  [SD0/7] |
      Servo Power | [5V/VIN]                 [CLK/6] |
                  +-----------------------------------+
                  
