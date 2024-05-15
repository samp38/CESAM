#include <bsp.h>
#include <Arduino.h>
#include "Arduino_BMI270_BMM150.h"

//************************************************************* STATE MACHINE STUFF ***************************************************************

class State
{
public:
    virtual ~State() {}
    virtual void enter() {}
    virtual State *run() = 0;
    virtual void exit() {}
};


class StartupState : public State
{
public:
    virtual void enter() override;
    virtual State *run() override;

private:
};

class BrakedState : public State
{
public:
    virtual void enter() override;
    virtual State *run() override;

private:
};

class UnbrakedState : public State
{
public:
    virtual void enter() override;
    virtual State *run() override;

private:
};

class OpeningState : public State
{
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual void exit() override;

private:
    uint64_t _lastProcessTime;
};

class ClosingState : public State
{
public:
    virtual void enter() override;
    virtual State *run() override;
    virtual void exit() override;

private:
    uint64_t _lastProcessTime;
};


void StartupState::enter()
{
    // AUTO Calibration
    delay(1000);
    Serial.println("StartupState::enter");
    delay(1000);

    // Check sensors

}

State* StartupState::run()
{
    // AUTO Calibration
    Serial.println("StartupState::run");
    delay(1000);

    // Check sensors
    return this;
}

StartupState startupState;

State *_state = &startupState;
State *_lastState = nullptr;


//****************************************************** MOTOR STUFF ******************************************************

void motor_stop(){
    digitalWrite(MOTOR_DIR1, LOW);
    digitalWrite(MOTOR_DIR2, LOW);
}

//****************************************************** ARDUINO STUFF ******************************************************


float x, y, z;
int plusThreshold = 30, minusThreshold = -30;

void setup(){
    Serial.begin(9600);
    pinMode(MOTOR_DIR1, OUTPUT);
    pinMode(MOTOR_DIR2, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    motor_stop();

    if (!IMU.begin()) {

    Serial.println("Failed to initialize IMU!");

    while (1);
    }
    Serial.print("Gyroscope sample rate = ");
    Serial.print(IMU.gyroscopeSampleRate());
    Serial.println(" Hz");
    Serial.println();
    Serial.println("Gyroscope in degrees/second");
}


void loop(){
//  if (_state == nullptr)
//     {
//         Serial.println("State machine terminated !!!");
//         while (true)
//             ;
//     }
//     if (_state != _lastState)
//     {
//         if (_lastState != nullptr)
//             _lastState->exit();
//         _state->enter();
//         _lastState = _state;
//     }
//     _state = _state->run();

if (IMU.gyroscopeAvailable()) {

    IMU.readGyroscope(x, y, z);
    Serial.println(z);
    }
}


