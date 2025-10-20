// motor_control.h
#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include <bsp.h>
#include "storage_hal.h"

// Public API
void Motor_Stop();
void Motor_Move(uint8_t direction, uint8_t speed = 255);

// Implementation
void Motor_Stop() {
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, LOW);
}

void Motor_Move(uint8_t direction, uint8_t speed) {
    int motorSpeed = Storage_GetSpeed();
    
    if (direction) {
        Serial.println("Motor_Move: Moving in direction 1 with speed " + String(motorSpeed));
        analogWrite(MOTOR_PIN1, motorSpeed);
        digitalWrite(MOTOR_PIN2, LOW);
    } else {
        Serial.println("Motor_Move: Moving in direction 0 with speed " + String(motorSpeed));
        digitalWrite(MOTOR_PIN1, LOW);
        analogWrite(MOTOR_PIN2, motorSpeed);
    }
}

#endif // MOTOR_CONTROL_H