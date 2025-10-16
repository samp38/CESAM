// ble_hal.h
#ifndef BLE_HAL_H
#define BLE_HAL_H

#include <Arduino.h>
#include "storage_hal.h"

#ifdef NANO_33_BLE
#include <ArduinoBLE.h>
#elif FEATHER_SENSE
#include <bluefruit.h>
#endif

// Public API
bool BLE_Init();
uint8_t BLE_CheckCommand();
void BLE_UpdateSpeed(int speed);

// Implementation
#ifdef NANO_33_BLE

static BLEService doorService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static BLEByteCharacteristic doorCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite);
static BLEByteCharacteristic speedCharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite | BLENotify);

void blePeripheralConnectHandler(BLEDevice central) {
    Serial.print("Connected event, central: ");
    Serial.println(central.address());
}

void speedCharacteristicWrittenHandler(BLEDevice central, BLECharacteristic characteristic) {
    Serial.println("Characteristic written : " + String(characteristic.uuid()));
    Serial.print("received value : ");
    int newSpeed = speedCharacteristic.value();
    Serial.println(newSpeed);
    Storage_SetSpeed(newSpeed);
    Storage_WritePrefs();
}

bool BLE_Init() {
    if (!BLE.begin()) {
        Serial.println("starting Bluetooth® Low Energy module failed!");
        return false;
    }
    
    BLE.setLocalName("CESAM");
    BLE.setAdvertisedService(doorService);
    doorService.addCharacteristic(doorCharacteristic);
    doorService.addCharacteristic(speedCharacteristic);
    BLE.addService(doorService);
    speedCharacteristic.writeValue(Storage_GetSpeed());
    BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
    speedCharacteristic.setEventHandler(BLEWritten, speedCharacteristicWrittenHandler);
    BLE.advertise();
    
    return true;
}

uint8_t BLE_CheckCommand() {
    BLE.poll();
    if (doorCharacteristic.written()) {
        return doorCharacteristic.value();
    }
    return 0;
}

void BLE_UpdateSpeed(int speed) {
    speedCharacteristic.writeValue(speed);
}

#elif FEATHER_SENSE

static BLEService doorService = BLEService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic doorCharacteristic = BLECharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic speedCharacteristic = BLECharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e");

static uint8_t lastDoorCommand = 0;

void blePeripheralConnectHandler(uint16_t conn_handle) {
    Serial.println("Connected event");
}

void speedCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    Serial.println("Speed characteristic written");
    if (len == 1) {
        int newSpeed = data[0];
        Serial.print("received value : ");
        Serial.println(newSpeed);
        Storage_SetSpeed(newSpeed);
        Storage_WritePrefs();
    }
}

void doorCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len == 1) {
        lastDoorCommand = data[0];
    }
}

bool BLE_Init() {
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName("CESAM");
    Bluefruit.Periph.setConnectCallback(blePeripheralConnectHandler);
    
    doorService.begin();
    
    doorCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    doorCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    doorCharacteristic.setFixedLen(1);
    doorCharacteristic.setWriteCallback(doorCharacteristicWrittenHandler);
    doorCharacteristic.begin();
    
    speedCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    speedCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    speedCharacteristic.setFixedLen(1);
    speedCharacteristic.setWriteCallback(speedCharacteristicWrittenHandler);
    speedCharacteristic.begin();
    speedCharacteristic.write8(Storage_GetSpeed());
    
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(doorService);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
    
    return true;
}

uint8_t BLE_CheckCommand() {
    if (lastDoorCommand != 0) {
        uint8_t cmd = lastDoorCommand;
        lastDoorCommand = 0; // Clear command after reading
        return cmd;
    }
    return 0;
}

void BLE_UpdateSpeed(int speed) {
    speedCharacteristic.write8(speed);
}

#endif

#endif // BLE_HAL_H