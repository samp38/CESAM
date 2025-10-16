// imu_hal.h
#ifndef IMU_HAL_H
#define IMU_HAL_H

#include <Arduino.h>

#ifdef NANO_33_BLE
#include "Arduino_BMI270_BMM150.h"
#elif FEATHER_SENSE
#include <Adafruit_LSM6DS33.h>
#endif

// Public API
bool IMU_Init();
bool IMU_GyroscopeAvailable();
void IMU_ReadGyroscope(float &x, float &y, float &z);

// Implementation
#ifdef NANO_33_BLE

bool IMU_Init() {
    if (!IMU.begin()) {
        Serial.println("Failed to initialize IMU!");
        return false;
    }
    Serial.print("Gyroscope sample rate = ");
    Serial.print(IMU.gyroscopeSampleRate());
    Serial.println(" Hz");
    Serial.println("Gyroscope in degrees/second");
    return true;
}

bool IMU_GyroscopeAvailable() {
    return IMU.gyroscopeAvailable();
}

void IMU_ReadGyroscope(float &x, float &y, float &z) {
    IMU.readGyroscope(x, y, z);
}

#elif FEATHER_SENSE

static Adafruit_LSM6DS33 lsm6ds33;

bool IMU_Init() {
    if (!lsm6ds33.begin_I2C()) {
        Serial.println("Failed to initialize LSM6DS33!");
        return false;
    }
    Serial.println("LSM6DS33 initialized");
    Serial.println("Gyroscope in degrees/second");
    return true;
}

bool IMU_GyroscopeAvailable() {
    return true; // LSM6DS33 always has data when polled
}

void IMU_ReadGyroscope(float &x, float &y, float &z) {
    sensors_event_t accel, gyro, temp;
    lsm6ds33.getEvent(&accel, &gyro, &temp);
    // Convert rad/s to deg/s (multiply by 180/PI = 57.2958)
    x = gyro.gyro.x * 57.2958f;
    y = gyro.gyro.y * 57.2958f;
    z = gyro.gyro.z * 57.2958f;
}

#endif

#endif // IMU_HAL_H