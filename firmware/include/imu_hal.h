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
static float gyro_offset_x = 0.0f;
static float gyro_offset_y = 0.0f;
static float gyro_offset_z = 0.0f;

bool IMU_Init() {
    if (!lsm6ds33.begin_I2C()) {
        Serial.println("Failed to initialize LSM6DS33!");
        return false;
    }
    Serial.println("LSM6DS33 initialized");
    
    // Calibration du gyroscope au repos
    Serial.println("========================================");
    Serial.println("Calibrating gyroscope...");
    Serial.println("KEEP THE BOARD STILL FOR 2 SECONDS!");
    Serial.println("========================================");
    delay(1000);
    
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    float sum_z = 0.0f;
    const int samples = 200;
    
    for (int i = 0; i < samples; i++) {
        sensors_event_t accel, gyro, temp;
        lsm6ds33.getEvent(&accel, &gyro, &temp);
        
        // Convertir rad/s en deg/s
        sum_x += gyro.gyro.x * 57.2958f;
        sum_y += gyro.gyro.y * 57.2958f;
        sum_z += gyro.gyro.z * 57.2958f;
        
        delay(10);
    }
    
    // Calculer les offsets moyens
    gyro_offset_x = sum_x / samples;
    gyro_offset_y = sum_y / samples;
    gyro_offset_z = sum_z / samples;
    
    Serial.println("Calibration complete!");
    Serial.print("Gyro offsets (deg/s): x=");
    Serial.print(gyro_offset_x, 2);
    Serial.print(" y=");
    Serial.print(gyro_offset_y, 2);
    Serial.print(" z=");
    Serial.println(gyro_offset_z, 2);
    Serial.println("========================================");
    Serial.println("Gyroscope ready - values in degrees/second");
    
    return true;
}

bool IMU_GyroscopeAvailable() {
    return true; // LSM6DS33 always has data when polled
}

void IMU_ReadGyroscope(float &x, float &y, float &z) {
    sensors_event_t accel, gyro, temp;
    lsm6ds33.getEvent(&accel, &gyro, &temp);
    
    // Convert rad/s to deg/s and apply calibration offsets
    x = (gyro.gyro.x * 57.2958f) - gyro_offset_x;
    y = (gyro.gyro.y * 57.2958f) - gyro_offset_y;
    z = (gyro.gyro.z * 57.2958f) - gyro_offset_z;
}

#endif

#endif // IMU_HAL_H