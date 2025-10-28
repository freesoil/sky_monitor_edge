#ifndef MOTOR_H
#define MOTOR_H

#include <Arduino.h>

/**
 * Motor class for modular motor control with LEDC (PWM) support
 * 
 * Features:
 * - Hardware PWM control using ESP32 LEDC
 * - Adjustable dead zone to prevent jitter
 * - Configurable maximum speed
 * - Supports forward and reverse operation
 * 
 * For XIAO ESP32S3 + DRV8833 motor driver:
 * - Recommended GPIO pins: 2, 4, 5, 7, 8, 9 (avoid 0-1, 3, 6, 10-21, 38-48 used by camera/UART)
 * - Example: Motor leftMotor(2, 4);
 *           Motor rightMotor(5, 7);
 */
class Motor {
private:
  int pin1, pin2;           // Motor driver pins
  int deadZone;             // Dead zone threshold
  int maxSpeed;             // Maximum motor speed
  
public:
  /**
   * Constructor
   * @param p1 Pin connected to motor driver input 1
   * @param p2 Pin connected to motor driver input 2
   * @param dead Dead zone threshold (default 15)
   * @param max Maximum motor speed (default 200)
   */
  Motor(int p1, int p2, int dead = 15, int max = 200);
  
  /**
   * Initialize motor pins and LEDC channels
   * Call this in setup() before using the motor
   */
  void init();
  
  /**
   * Control motor with speed
   * @param speed Motor speed from -255 to 255
   *              Negative values = reverse
   *              0 = stop
   */
  void setSpeed(int speed);
  
  /**
   * Stop the motor immediately
   */
  void stop();
  
  /**
   * Print motor configuration for debugging
   * @param name Motor identifier (e.g., "Left", "Right")
   */
  void printInfo(const char* name);
};

#endif // MOTOR_H
