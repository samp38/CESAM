#include <bsp.h>
#include <Arduino.h>
#include "Arduino_BMI270_BMM150.h"
#include <Smoothed.h>
#include <Servo.h>


#define VZ_TH  0.3f
#define VZ_TH_MOVE  1.0f


//****************************************************** SERVO STUFF ******************************************************

Servo myservo;

int pos = 0;    // variable to store the servo position


//****************************************************** FILTERS STUFF ******************************************************

Smoothed <float> smoothingCurrentFilter;
void motor_stop();
void motor_move(uint8_t direction, uint8_t speed=255);
void activeBreak();
void deactiveBreak();

float x, y, z;

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


StartupState startupState;
BrakedState brakedState;
UnbrakedState unbrakedState;
OpeningState openingState;
ClosingState closingState;

State *_state = &startupState;
State *_lastState = nullptr;



void StartupState::enter()
{
    // AUTO Calibration
    // delay(1000);
    Serial.println("StartupState::enter");
    // delay(1000);
}

State* StartupState::run()
{
    Serial.println("StartupState::run");
    // delay(1000);
    // Start filter
    smoothingCurrentFilter.begin(SMOOTHED_AVERAGE, 100);

    // Start IMU
    if (!IMU.begin()) {
        Serial.println("Failed to initialize IMU!");
        delay(500);
        while (1);
    }
    Serial.print("Gyroscope sample rate = ");
    Serial.print(IMU.gyroscopeSampleRate());
    Serial.println(" Hz");
    Serial.println();
    Serial.println("Gyroscope in degrees/second");

    // Stop MOTOR
    motor_stop();

    return &brakedState;
}

void BrakedState::enter() {
    Serial.println("BrakedState::enter");
    motor_stop();
    activeBreak();
}

State* BrakedState::run() {
    // Here we check gyrovalues. If we detect a move, we make the door free
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
        if(abs(z) > 1.) {
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
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
        if(abs(z) > VZ_TH) {
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
    motor_move(0);
    movement_timer = millis();
}

State* OpeningState::run() {
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
        if(abs(z) > VZ_TH_MOVE) {
            autoBreak_timer = millis();
        }
    }
    if (millis() - movement_timer > 2000) {
        return &brakedState;
    }
    return this;
}

void OpeningState::exit() {
    Serial.println("OpeningState::exit");
    motor_stop();
}


void ClosingState::enter() {
    Serial.println("ClosingState::enter");
    deactiveBreak();
    motor_move(1);
    movement_timer = millis();
}

State* ClosingState::run() {
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
        if(abs(z) > VZ_TH_MOVE) {
            autoBreak_timer = millis();
        }
    }
    if (millis() - movement_timer > 2000) {
        return &brakedState;
    }
    return this;
}

void ClosingState::exit() {
    Serial.println("ClosingState::exit");
    motor_stop();
}

//****************************************************** MOTOR & BREAKE STUFF ******************************************************

void motor_stop(){
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, LOW);
}

void motor_move(uint8_t direction, uint8_t speed) {
    if(direction) {
        digitalWrite(MOTOR_PIN1, HIGH);
        digitalWrite(MOTOR_PIN2, LOW);
    } else {
        digitalWrite(MOTOR_PIN1, LOW);
        digitalWrite(MOTOR_PIN2, HIGH);
    }
}

void activeBreak() {
    // Move servo so breaking position
    // for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
        // in steps of 1 degree
        myservo.write(160);              // tell servo to go to position in variable 'pos'
    // }
}

void deactiveBreak() {
    // Move servo to free position
    // for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
        myservo.write(20);              // tell servo to go to position in variable 'pos'
}

//****************************************************** ARDUINO STUFF ******************************************************

void setup(){
    Serial.begin(9600);
    pinMode(MOTOR_PIN1, OUTPUT);
    pinMode(MOTOR_PIN2, OUTPUT);
    pinMode(MOTOR_PIN2, OUTPUT);
    pinMode(SERVO_PIN, OUTPUT);

    myservo.attach(SERVO_PIN);
    digitalWrite(LED_BUILTIN, LOW);
}


void loop(){
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


