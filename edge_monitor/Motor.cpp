#include "Motor.h"
#include <Arduino.h>

// Constructor
Motor::Motor(int p1, int p2, int ch1, int ch2, int dead, int max) 
  : pin1(p1), pin2(p2), channel1(ch1), channel2(ch2), deadZone(dead), maxSpeed(max) {}

// Initialize motor pins and LEDC channels
void Motor::init() {
  pinMode(pin1, OUTPUT);
  pinMode(pin2, OUTPUT);
  
  // ESP32 3.0.x LEDC API: ledcAttach(pin, frequency, resolution)
  // Channel is automatically assigned
  ledcAttach(pin1, 4000, 8);  // 4kHz, 8-bit resolution
  ledcAttach(pin2, 4000, 8);  // 4kHz, 8-bit resolution
  
  stop();  // Initialize to stop
}

// Control motor with speed (-255 to 255, negative = reverse)
void Motor::setSpeed(int speed) {
  // Apply dead zone
  if (abs(speed) < deadZone) {
    stop();
    return;
  }
  
  // Clamp speed to max range
  speed = constrain(speed, -maxSpeed, maxSpeed);
  
  // Convert to absolute speed for PWM
  int pwmSpeed = map(abs(speed), deadZone, maxSpeed, 0, 255);
  
  if (speed > 0) {
    // Forward: pin1 LOW, pin2 PWM
    ledcWrite(pin1, 0);
    ledcWrite(pin2, pwmSpeed);
  } else {
    // Reverse: pin1 PWM, pin2 LOW
    ledcWrite(pin1, pwmSpeed);
    ledcWrite(pin2, 0);
  }
}

// Stop motor
void Motor::stop() {
  ledcWrite(pin1, 0);
  ledcWrite(pin2, 0);
}

// Get motor info for debugging
void Motor::printInfo(const char* name) {
  Serial.printf("%s Motor: pin1=%d, pin2=%d, ch1=%d, ch2=%d\n", 
                name, pin1, pin2, channel1, channel2);
}
