#include <bsp.h>
#include <Arduino.h>
#include "imu_hal.h"
#include "ble_hal.h"
#include "storage_hal.h"
#include "motor_control.h"

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

class StoppedState : public State {
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
StoppedState stoppedState;
OpeningState openingState;
ClosingState closingState;

State *_state = &startupState;
State *_lastState = nullptr;
unsigned long last_processing_time;

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
    return &stoppedState;
}

void StoppedState::enter() {
    Serial.println("StoppedState::enter");
    Motor_Stop();
    delay(1000);
}

State* StoppedState::run() {
    if (millis() - last_processing_time < LOOP_TIME_MS) {return this;}
    last_processing_time = millis();
    State* bt_next_state = check_bt_command();
    if (bt_next_state != nullptr) {
        return bt_next_state;
    }
    return this;
}


unsigned long movement_timer;
void OpeningState::enter() {
    Serial.println("OpeningState::enter");
    Motor_Move(0, Storage_GetSpeed());
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
        return &stoppedState;
    }
    return this;
}

void OpeningState::exit() {
    Serial.println("OpeningState::exit");
    Motor_Stop();
}

void ClosingState::enter() {
    Serial.println("ClosingState::enter");
    Motor_Move(1, Storage_GetSpeed());
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
        return &stoppedState;
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
    long start_time = millis();
    while (!Serial && (millis() - start_time < 10000)) {
        ; // wait for serial port to connect. Needed for native USB
    }
    Serial.println("=== NanoBLE Door Opener ===");

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
    Storage_Process();
}