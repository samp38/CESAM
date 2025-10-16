#include <bsp.h>
#include <Arduino.h>
#include "imu_hal.h"
#include "ble_hal.h"
#include "storage_hal.h"
#include "motor_control.h"
#include "servo_hal.h"

#define VZ_TH  2.0f
#define VZ_TH_MOVE  1.0f
#define LOOP_TIME_MS 100

//****************************************************** STATE MACHINE ******************************************************

float x, y, z;

class State {
public:
    virtual ~State() {}
    virtual void enter() {}
    virtual State *run() = 0;
    virtual void exit() {}
};

class StartupState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
};

class BrakedState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
};

class UnbrakedState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
};

class OpeningState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual void exit() override;
private:
    uint64_t _lastProcessTime;
};

class ClosingState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual void exit() override;
private:
    uint64_t _lastProcessTime;
};

State* check_bt_command();

StartupState startupState;
BrakedState brakedState;
UnbrakedState unbrakedState;
OpeningState openingState;
ClosingState closingState;

State *_state = &startupState;
State *_lastState = nullptr;
unsigned long last_processing_time;

void activeBreak() {
    Servo_Write(75);
}

void deactiveBreak() {
    Servo_Write(30);
}

void StartupState::enter() {
    Serial.println("StartupState::enter");
}

State* StartupState::run() {
    Serial.println("StartupState::run");
    
    if (!IMU_Init()) {
        delay(500);
        while (1);
    }

    Serial.println("Read preference record...");
    if (!Storage_ReadPrefs()) {
        Serial.println("No preferences found.");
        Serial.println("Setting default settings values");
        Storage_SetDefaults();
        Storage_WritePrefs();
        if (!Storage_ReadPrefs()) {
            Serial.println("Cannot write to flash storage");
            while(1);
        }
    }
    Serial.println("");

    Motor_Stop();

    if (!BLE_Init()) {
        Serial.println("BLE initialization failed!");
        while (1);
    }

    Serial.println("Bluetooth® device active, waiting for connections...");
    return &brakedState;
}

void BrakedState::enter() {
    Serial.println("BrakedState::enter");
    Motor_Stop();
    activeBreak();
    delay(1000);
}

State* BrakedState::run() {
    if (millis() - last_processing_time < LOOP_TIME_MS) {return this;}
    last_processing_time = millis();
    State* bt_next_state = check_bt_command();
    if (bt_next_state != nullptr) {
        return bt_next_state;
    }
    if (IMU_GyroscopeAvailable()) {
        IMU_ReadGyroscope(x, y, z);
        if(abs(y) + abs(x) + abs(z) > VZ_TH) {
            return &unbrakedState;
        }
    }
    return this;
}

unsigned long autoBreak_timer;
void UnbrakedState::enter() {
    Serial.println("UnBrakedState::enter");
    autoBreak_timer = millis();
    deactiveBreak();
}

State* UnbrakedState::run() {
    if (millis() - last_processing_time < LOOP_TIME_MS) {return this;}
    last_processing_time = millis();
    State* bt_next_state = check_bt_command();
    if (bt_next_state != nullptr) {
        return bt_next_state;
    }
    if (IMU_GyroscopeAvailable()) {
        IMU_ReadGyroscope(x, y, z);
        if(abs(y) > VZ_TH) {
            autoBreak_timer = millis();
        }
    }
    if (millis() - autoBreak_timer > 3000) {
        return &brakedState;
    }
    return this;
}

unsigned long movement_timer;
void OpeningState::enter() {
    Serial.println("OpeningState::enter");
    deactiveBreak();
    Motor_Move(0);
    delay(500);
    movement_timer = millis();
}

State* OpeningState::run() {
    if (millis() - last_processing_time < LOOP_TIME_MS) {return this;}
    last_processing_time = millis();
    State* bt_next_state = check_bt_command();
    if (bt_next_state != nullptr) {
        return bt_next_state;
    }
    if (IMU_GyroscopeAvailable()) {
        IMU_ReadGyroscope(x, y, z);
        if(abs(y) > VZ_TH_MOVE) {
            movement_timer = millis();
        }
    }
    if (millis() - movement_timer > 1000) {
        return &brakedState;
    }
    return this;
}

void OpeningState::exit() {
    Serial.println("OpeningState::exit");
    Motor_Stop();
}

void ClosingState::enter() {
    Serial.println("ClosingState::enter");
    deactiveBreak();
    Motor_Move(1);
    delay(500);
    movement_timer = millis();
}

State* ClosingState::run() {
    if (millis() - last_processing_time < LOOP_TIME_MS) {return this;}
    last_processing_time = millis();
    State* bt_next_state = check_bt_command();
    if (bt_next_state != nullptr) {
        return bt_next_state;
    }
    if (IMU_GyroscopeAvailable()) {
        IMU_ReadGyroscope(x, y, z);
        if(abs(y) > VZ_TH_MOVE) {
            movement_timer = millis();
        }
    }
    if (millis() - movement_timer > 1000) {
        return &brakedState;
    }
    return this;
}

void ClosingState::exit() {
    Serial.println("ClosingState::exit");
    Motor_Stop();
}

State* check_bt_command() {
    uint8_t cmd = BLE_CheckCommand();
    if (cmd == '0') {
        return &openingState;
    } else if (cmd == '1') {
        return &closingState;
    } else if (cmd == '2') {
        Serial.println("REFRESH");
        BLE_UpdateSpeed(Storage_GetSpeed());
        return nullptr;
    }
    return nullptr;
}

void setup() {
    Serial.begin(9600);
    pinMode(MOTOR_PIN1, OUTPUT);
    pinMode(MOTOR_PIN2, OUTPUT);
    pinMode(SERVO_PIN, OUTPUT);

    Servo_Init(SERVO_PIN);
    digitalWrite(LED_BUILTIN, LOW);
    
    Storage_Init();
}

void loop() {
    if (_state == nullptr) {
        Serial.println("State machine terminated !!!");
        while (true);
    }
    if (_state != _lastState) {
        if (_lastState != nullptr) {
            _lastState->exit();
        }        
        _state->enter();
        _lastState = _state;
    }
    _state = _state->run();
}