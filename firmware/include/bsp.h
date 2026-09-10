// bsp.h - Board Support Package
#ifndef BSP_H
#define BSP_H

// Pin definitions for different boards
#ifdef NANO_33_BLE
  #define MOTOR_PIN1 D2
  #define MOTOR_PIN2 D3
  #define MOTOR_PWM D5
#elif FEATHER_SENSE
  #define MOTOR_PIN1 A0
  #define MOTOR_PIN2 A1
  // Added so the target keeps building after speed control was introduced; this pin
  // has not been checked against an actual Feather wiring.
  #define MOTOR_PWM A2
#else
  #error "Board not supported! Define NANO_33_BLE or FEATHER_SENSE"
#endif

#endif // BSP_H