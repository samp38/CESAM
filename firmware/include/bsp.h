// bsp.h - Board Support Package
#ifndef BSP_H
#define BSP_H

// Pin definitions for different boards
#ifdef NANO_33_BLE
  #define MOTOR_PIN1 D2
  #define MOTOR_PIN2 D3
#elif FEATHER_SENSE
  #define MOTOR_PIN1 A0
  #define MOTOR_PIN2 A1
#else
  #error "Board not supported! Define NANO_33_BLE or FEATHER_SENSE"
#endif

#endif // BSP_H