#include <bsp.h>
#include <Arduino.h>
#include "imu_hal.h"
#include "ble_hal.h"
#include "storage_hal.h"
#include "motor_control.h"

#define VZ_TH  2.0f
#define VZ_TH_MOVE  6.0f
#define LOOP_TIME_MS 100

//****************************************************** STATE MACHINE ******************************************************

// Global gyroscope readings
float x, y, z;

class State {
public:
    virtual ~State() {}
    virtual void enter() {}
    virtual State *run() = 0;
    virtual void exit() {}
    // value reported to the app on the state characteristic
    virtual uint8_t id() const = 0;
};

class StartupState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual uint8_t id() const override { return DOOR_STATE_STARTUP; }
};

// The door at rest. The four instances below differ only by what they report to the app,
// so they share one class: the state the machine transitions to *is* the reason it
// stopped, and there is no separate field to keep in sync.
class RestingState : public State {
public:
    explicit RestingState(uint8_t id) : _id(id) {}
    virtual void enter() override;
    virtual State *run() override;
    virtual uint8_t id() const override { return _id; }
private:
    uint8_t _id;
};

class OpeningState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual void exit() override;
    virtual uint8_t id() const override { return DOOR_STATE_OPENING; }
private:
    uint64_t _lastProcessTime;
};

class ClosingState : public State {
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual void exit() override;
    virtual uint8_t id() const override { return DOOR_STATE_CLOSING; }
private:
    uint64_t _lastProcessTime;
};

State* check_bt_command();

StartupState startupState;
RestingState unknownState(DOOR_STATE_UNKNOWN);
RestingState openState(DOOR_STATE_OPEN);
RestingState closedState(DOOR_STATE_CLOSED);
RestingState pausedState(DOOR_STATE_PAUSED);
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
    // nothing has moved yet, so the position is genuinely unknown
    return &unknownState;
}

void RestingState::enter() {
    Serial.print("RestingState::enter, id=");
    Serial.println(_id);
    Motor_Stop();
    delay(1000);
}

State* RestingState::run() {
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
    if (IMU_AccelerometerAvailable()) { //testing the availability of IMU data. Due to a known issue on Arduino Nano 33 BLE Rev 2 (the Gyroscope available flag stays FALSE), we test here the acceleration available flag.
        IMU_ReadGyroscope(x, y, z);
        float total_rot = abs(x) + abs(y) + abs(z);
        if(total_rot > VZ_TH_MOVE) {
            movement_timer = millis();
        }
    }
    if (millis() - movement_timer > 1000) {
        // the door stopped turning, so the opening travel ran to completion
        return &openState;
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
    if (IMU_AccelerometerAvailable()) { //testing the availability of IMU data. Due to a known issue on Arduino Nano 33 BLE Rev 2 (the Gyroscope available flag stays FALSE), we test here the acceleration available flag.
        IMU_ReadGyroscope(x, y, z);
        float total_rot = abs(x) + abs(y) + abs(z);
        if(total_rot > VZ_TH_MOVE) {
            movement_timer = millis();
        }
    }
    if (millis() - movement_timer > 1000) {
        // the door stopped turning, so the closing travel ran to completion
        return &closedState;
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
        // the app has no other way of learning the current state right after connecting:
        // a transition may not happen for a long time
        BLE_UpdateState(_state->id());
        return nullptr;
    } else if (cmd == '3') {
        Serial.println("PAUSE");
        // RestingState::enter stops the motor, as does the exit() of the moving states.
        // The travel was interrupted, so the door is somewhere in between.
        return &pausedState;
    }
    return nullptr;
}

void setup() {
    Serial.begin(9600);
    pinMode(MOTOR_PIN1, OUTPUT);
    pinMode(MOTOR_PIN2, OUTPUT);
    pinMode(MOTOR_PWM, OUTPUT);
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
        // Single transition point of the state machine, so notifying here reports every
        // change - including the ones the app did not ask for, such as the end of travel
        // detected by the IMU.
        BLE_UpdateState(_state->id());
    }
    _state = _state->run();
    Storage_Process();
}