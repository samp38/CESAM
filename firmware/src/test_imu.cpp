#include <Arduino.h>
#include "imu_hal.h"



void setup() {
    Serial.begin(9600);    
    long start_time = millis();
    while (!Serial && (millis() - start_time < 10000)) {
        ; // wait for serial port to connect. Needed for native USB
    }
    Serial.println("=== NanoBLE Door Opener -- IMU TESTING ===");

    digitalWrite(LED_BUILTIN, LOW);
    
    if (!IMU_Init()) {
        while (1) {
            Serial.println("IMU_Init failed !");
            delay(1000);
        };
    }
    Serial.println("IMU_Init succeeded !");
}

void loop() {
    float x, y, z;
    if (IMU_GyroscopeAvailable()) {
        IMU_ReadGyroscope(x, y, z);
        Serial.print("Gyro (deg/s): X=");
        Serial.print(x, 2);
        Serial.print(" Y=");
        Serial.print(y, 2);
        Serial.print(" Z=");
        Serial.println(z, 2);
    } else {
        Serial.println("Gyro data not available");
    }
    delay(200);
}