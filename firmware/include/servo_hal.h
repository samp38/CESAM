#ifndef SERVO_HAL_H
#define SERVO_HAL_H

#include <Arduino.h>
#include <bsp.h>

#ifdef NANO_33_BLE
#include <Servo.h>
static Servo myservo;

void Servo_Init(uint8_t pin) {
    myservo.attach(pin);
}

void Servo_Write(int angle) {
    myservo.write(angle);
}

#elif FEATHER_SENSE
#include "nRF52_PWM.h"

// Create PWM instance for servo control
static nRF52_PWM* servo_pwm = nullptr;
static uint8_t servo_pin;

void Servo_Init(uint8_t pin) {
    servo_pin = pin;
    
    // Create PWM at 50Hz (standard servo frequency)
    // Period = 20ms = 20000us
    servo_pwm = new nRF52_PWM(pin, 50.0f, 0.0f);  // pin, frequency, initial duty cycle
    
    if (servo_pwm) {
        // Start with neutral position (1.5ms = 90 degrees)
        float dutyCycle = (1500.0f / 20000.0f) * 100.0f;  // 7.5%
        servo_pwm->setPWM(pin, 50.0f, dutyCycle);
    }
}

void Servo_Write(int angle) {
    if (!servo_pwm) return;
    
    // Convert angle (0-180) to pulse width (1000-2000 us)
    // Standard servo: 1ms = 0°, 1.5ms = 90°, 2ms = 180°
    float pulseWidth = map(angle, 0, 180, 1000, 2000);
    
    // Calculate duty cycle: (pulse_width / period) * 100
    // Period = 20ms = 20000us at 50Hz
    float dutyCycle = (pulseWidth / 20000.0f) * 100.0f;
    
    servo_pwm->setPWM(servo_pin, 50.0f, dutyCycle);
}
#endif

#endif // SERVO_HAL_H