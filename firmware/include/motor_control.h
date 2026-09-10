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
    Serial.println("Motor_Stop: Stopping motor");
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, LOW);
}

void Motor_Move(uint8_t direction, uint8_t speed) {
    int motorSpeed = Storage_GetSpeed();

    // Both OpeningState and ClosingState come through here, so this is the one place
    // that has to know about a motor wired the other way round.
    if (Storage_GetReversed()) {
        direction = !direction;
    }

    if (direction) {
        Serial.println("Motor_Move: Moving in direction 1 with speed " + String(motorSpeed));
        digitalWrite(MOTOR_PIN1, LOW);
        digitalWrite(MOTOR_PIN2, HIGH);
        analogWrite(MOTOR_PWM, motorSpeed);
    } else {
        Serial.println("Motor_Move: Moving in direction 0 with speed " + String(motorSpeed));
        digitalWrite(MOTOR_PIN1, HIGH);
        digitalWrite(MOTOR_PIN2, LOW);
        analogWrite(MOTOR_PWM, motorSpeed);
    }
}

#endif // MOTOR_CONTROL_H