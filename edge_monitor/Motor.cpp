#include <Arduino.h>
#include "Motor.h"

static const uint32_t FREQ = 20000; // 20 kHz
static const uint8_t  RES  = 8;     // 0..255

// Constructor: pins + deadzone + maxSpeed
Motor::Motor(int p1, int p2, int dead, int max)
  : pin1(p1), pin2(p2), deadZone(dead), maxSpeed(max) {}

void Motor::init() {
  // Attach PWM to both pins (auto channel selection)
  bool ok1 = ledcAttach(pin1, FREQ, RES);
  bool ok2 = ledcAttach(pin2, FREQ, RES);

  if (!ok1 || !ok2) {
    Serial.println("ERROR: ledcAttach failed on one or both pins");
  } else {
    Serial.printf("****** ledcAttach successful on pin1=%d, pin2=%d ******\n", pin1, pin2);
  }
  stop();
}

void Motor::setSpeed(int speed) {
  if (abs(speed) < deadZone) { stop(); return; }

  speed = constrain(speed, -maxSpeed, maxSpeed);

  int denom = max(1, maxSpeed - deadZone);
  int pwm   = ( (int)abs(speed) - deadZone ) * 255 / denom;
  pwm       = constrain(pwm, 0, 255);

  if (speed > 0) {
    // CCW: IN1 low, IN2 PWM
    ledcWrite(pin1, 0);
    ledcWrite(pin2, pwm);
  } else {
    // CW: IN1 PWM, IN2 low
    ledcWrite(pin1, pwm);
    ledcWrite(pin2, 0);
  }
}

void Motor::stop() {
  ledcWrite(pin1, 0);
  ledcWrite(pin2, 0);
}

void Motor::printInfo(const char* name) {
  Serial.printf("%s Motor: pin1=%d, pin2=%d\n", name, pin1, pin2);
}
