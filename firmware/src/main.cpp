#include <bsp.h>
#include <Arduino.h>
#ifdef NANO_33_BLE
#include "Arduino_BMI270_BMM150.h"
#include <Servo.h>
#include <ArduinoBLE.h>
#include <NanoBLEFlashPrefs.h>
#elif FEATHER_SENSE

#endif

#define DEFAULT_SPEED 255           // for eeprom setting
#define DEFAULT_NAME "CESAM_DOOR"   // for eeprom setting

#define VZ_TH  2.0f
#define VZ_TH_MOVE  1.0f
#define LOOP_TIME_MS 100

//****************************************************** EEPROM STUFF ***************************************************


NanoBLEFlashPrefs myFlashPrefs;

// Structure of preferences. You determine the fields.
// Must not exeed 1019 words (4076 byte).
typedef struct flashStruct
{
  char pref_doorName[64];
  int speed;
} flashPrefs;

flashPrefs globalPrefs;

void printPreferences(flashPrefs thePrefs)
{
  Serial.println("Preferences: ");
  Serial.println(thePrefs.pref_doorName);
  Serial.println(thePrefs.speed);
}

// Print return code infos to Serial.
void printReturnCode(int rc)
{
  Serial.print("Return code: ");
  Serial.print(rc);
  Serial.print(", ");
  Serial.println(myFlashPrefs.errorString(rc));
}

//****************************************************** BLE STUFF ******************************************************

BLEService doorService("6e400001-b5a3-f393-e0a9-e50e24dcca9e"); // create service

// create switch characteristic and allow remote device to read and write
BLEByteCharacteristic doorCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite);
BLEByteCharacteristic speedCharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite | BLENotify);

void blePeripheralConnectHandler(BLEDevice central) {
    Serial.print("Connected event, central: ");
    Serial.println(central.address());
}


void speedCharacteristicWrittenHandler(BLEDevice central, BLECharacteristic characteristic) {
    Serial.println("Characteristic written : " + String(characteristic.uuid()));
    Serial.print("received value : ");
    int newSpeed = speedCharacteristic.value();
    Serial.println(newSpeed);
    globalPrefs.speed = newSpeed;
    myFlashPrefs.writePrefs(&globalPrefs, sizeof(globalPrefs));
}


//****************************************************** SERVO STUFF ******************************************************

Servo myservo;

int pos = 0;    // variable to store the servo position


//****************************************************** FILTERS STUFF ******************************************************

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

State* check_bt_command();

StartupState startupState;
BrakedState brakedState;
UnbrakedState unbrakedState;
OpeningState openingState;
ClosingState closingState;

State *_state = &startupState;
State *_lastState = nullptr;
unsigned long last_processing_time;


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

    // Check (fake)EEPROM settings
    // See if we already have a preference record
    Serial.println("Read preference record...");
    int rc = myFlashPrefs.readPrefs(&globalPrefs, sizeof(globalPrefs));
    if (rc == FDS_SUCCESS)
    {
        printPreferences(globalPrefs);
    }
    else
    {
        Serial.println("No preferences found."); // This should be the case when running for the first time on that particular board
        printReturnCode(rc);
        Serial.println("Setting default settings values into EEPROM");
        globalPrefs.speed = DEFAULT_SPEED;
        strcpy(globalPrefs.pref_doorName, DEFAULT_NAME);
        myFlashPrefs.writePrefs(&globalPrefs, sizeof(globalPrefs));
        rc = myFlashPrefs.readPrefs(&globalPrefs, sizeof(globalPrefs));
        if (rc == FDS_SUCCESS)
        {
            printPreferences(globalPrefs);
        }
        else
        {
            printReturnCode(rc);
            Serial.println("");
            Serial.println("Cannot write to fake EEPROM");
            while(1) {;}
        }
        
    }
    Serial.println("");

    // Stop MOTOR
    motor_stop();


    // START BLE
    if (!BLE.begin()) {
    Serial.println("starting Bluetooth® Low Energy module failed!");

    while (1);
    }

    // set the local name peripheral advertises
    BLE.setLocalName("CESAM");
    // set the UUID for the service this peripheral advertises:
    BLE.setAdvertisedService(doorService);

    // add the characteristics to the service
    doorService.addCharacteristic(doorCharacteristic);
    doorService.addCharacteristic(speedCharacteristic);

    // add the service
    BLE.addService(doorService);

    // doorCharacteristic.writeValue(0);
    speedCharacteristic.writeValue(globalPrefs.speed);

    // Set Callbacks
    BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
    speedCharacteristic.setEventHandler(BLEWritten, speedCharacteristicWrittenHandler);

    // start advertising
    BLE.advertise();

    Serial.println("Bluetooth® device active, waiting for connections...");

    return &brakedState;
}

void BrakedState::enter() {
    Serial.println("BrakedState::enter");
    motor_stop();
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
    // Here we check gyrovalues. If we detect a move, we make the door free
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
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
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
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
    motor_move(0);
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
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
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
    motor_stop();
}


void ClosingState::enter() {
    Serial.println("ClosingState::enter");
    deactiveBreak();
    motor_move(1);
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
    if (IMU.gyroscopeAvailable()) {
        IMU.readGyroscope(x, y, z);
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
    motor_stop();
}

//****************************************************** COMMON BLE STUFF **********************************************************

State* check_bt_command() {
    BLE.poll();
    if (doorCharacteristic.written()) {
        if (doorCharacteristic.value() == 48 ) {
            return &openingState;
        } else if(doorCharacteristic.value() == 49) {
            return &closingState;
        }
    }
    else {
        return nullptr;
    }
}



//****************************************************** MOTOR & BREAKE STUFF ******************************************************

void motor_stop(){
    digitalWrite(MOTOR_PIN1, LOW);
    digitalWrite(MOTOR_PIN2, LOW);
}

void motor_move(uint8_t direction, uint8_t speed) {
    if(direction) {
        analogWrite(MOTOR_PIN1, globalPrefs.speed);
        digitalWrite(MOTOR_PIN2, LOW);
    } else {
        digitalWrite(MOTOR_PIN1, LOW);
        analogWrite(MOTOR_PIN2, globalPrefs.speed);
    }
}

void activeBreak() {
    // Move servo so breaking position
    // for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
        // in steps of 1 degree
        myservo.write(75);              // tell servo to go to position in variable 'pos'
    // }
}

void deactiveBreak() {
    // Move servo to free position
    // for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
        myservo.write(30);              // tell servo to go to position in variable 'pos'
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


