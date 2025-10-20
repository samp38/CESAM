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
    // Storage_WritePrefs();
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
static bool clientSubscribed = false;

void blePeripheralConnectHandler(uint16_t conn_handle) {
    Serial.println("Connected event");
    clientSubscribed = false;  // Reset sur nouvelle connexion
}

void blePeripheralDisconnectHandler(uint16_t conn_handle, uint8_t reason) {
    Serial.println("Disconnected event");
    clientSubscribed = false;
}

void speedCccdCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint16_t value) {
    Serial.print("Speed CCCD updated: ");
    Serial.println(value);
    
    if (value & 0x0001) {
        Serial.println("Client SUBSCRIBED to notifications");
        clientSubscribed = true;
        // Envoyer immédiatement la vitesse quand le client s'abonne
        delay(100);
        BLE_UpdateSpeed(Storage_GetSpeed());
    } else {
        Serial.println("Client UNSUBSCRIBED from notifications");
        clientSubscribed = false;
    }
}

void speedCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    Serial.print("Speed characteristic written, length: ");
    Serial.println(len);
    
    if (len == 1) {
        int newSpeed = data[0];
        Serial.print("Received speed value (1 byte): ");
        Serial.println(newSpeed);
        Storage_SetSpeed(newSpeed);
        Storage_WritePrefs();
    } else if (len == 2) {
        // Si l'app envoie 2 bytes (big endian)
        int newSpeed = (data[0] << 8) | data[1];
        Serial.print("Received speed value (2 bytes): ");
        Serial.println(newSpeed);
        Storage_SetSpeed(newSpeed);
        Storage_WritePrefs();
    } else {
        Serial.print("Unexpected length: ");
        Serial.println(len);
    }
}

void doorCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len == 1) {
        lastDoorCommand = data[0];
        Serial.print("Door command received: ");
        Serial.println((char)lastDoorCommand);
    }
}

bool BLE_Init() {
    Serial.println("BLE_Init: Starting Bluefruit...");
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName("CESAM");
    Bluefruit.Periph.setConnectCallback(blePeripheralConnectHandler);
    Bluefruit.Periph.setDisconnectCallback(blePeripheralDisconnectHandler);
    
    Serial.println("BLE_Init: Starting door service...");
    doorService.begin();
    
    doorCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    doorCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    doorCharacteristic.setFixedLen(1);
    doorCharacteristic.setWriteCallback(doorCharacteristicWrittenHandler);
    doorCharacteristic.begin();
    uint8_t doorInit = 0;
    doorCharacteristic.write(&doorInit, 1);
    Serial.println("BLE_Init: Door characteristic configured");
    
    // Configuration de la caractéristique de vitesse avec notification
    speedCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    speedCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    speedCharacteristic.setFixedLen(2);
    speedCharacteristic.setWriteCallback(speedCharacteristicWrittenHandler);
    speedCharacteristic.setCccdWriteCallback(speedCccdCallback);  // ← IMPORTANT !
    speedCharacteristic.begin();
    
    // Écrire la vitesse initiale (2 bytes)
    uint16_t initialSpeed = Storage_GetSpeed();
    Serial.print("BLE_Init: Initial speed from storage: ");
    Serial.println(initialSpeed);
    
    uint8_t speedBytes[2];
    speedBytes[0] = (initialSpeed >> 8) & 0xFF;
    speedBytes[1] = initialSpeed & 0xFF;
    speedCharacteristic.write(speedBytes, 2);
    
    Serial.print("BLE_Init: Speed bytes written: [");
    Serial.print(speedBytes[0]);
    Serial.print(", ");
    Serial.print(speedBytes[1]);
    Serial.println("]");
    
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(doorService);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
    
    Serial.println("BLE_Init: Advertising started");
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
    if (!clientSubscribed) {
        Serial.println("BLE_UpdateSpeed: Client not subscribed, skipping notification");
        return;
    }
    
    // Envoyer 2 bytes (big endian) pour être compatible avec fromBytes()
    uint8_t speedBytes[2];
    speedBytes[0] = (speed >> 8) & 0xFF;  // MSB
    speedBytes[1] = speed & 0xFF;         // LSB
    
    bool success = speedCharacteristic.notify(speedBytes, 2);
    
    Serial.print("Speed notification sent: ");
    Serial.print(speed);
    Serial.print(" [");
    Serial.print(speedBytes[0]);
    Serial.print(", ");
    Serial.print(speedBytes[1]);
    Serial.print("] - ");
    Serial.println(success ? "SUCCESS" : "FAILED");
}

#endif

#endif // BLE_HAL_H