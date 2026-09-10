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

// Door state reported to the app on characteristic ...0005, as a single raw byte.
// Unlike the command characteristic, which carries ASCII, this is a plain enum.
//
// The resting states say why the door stopped moving, which is all the board knows:
// OPEN and CLOSED mean a travel ran to completion, not a position read from a sensor.
// There is no limit switch - the end of travel is inferred from the IMU no longer
// seeing rotation - so a door jammed mid-travel also reports OPEN or CLOSED.
#define DOOR_STATE_STARTUP 0
#define DOOR_STATE_UNKNOWN 1  // at rest, but no travel has completed since boot
#define DOOR_STATE_OPEN    2
#define DOOR_STATE_CLOSED  3
#define DOOR_STATE_PAUSED  4  // stopped part-way by a pause command
#define DOOR_STATE_OPENING 5
#define DOOR_STATE_CLOSING 6

// Public API
bool BLE_Init();
uint8_t BLE_CheckCommand();
void BLE_UpdateSpeed(int speed);
void BLE_UpdateName(const char* name);
void BLE_UpdateState(uint8_t state);

// Implementation
#ifdef NANO_33_BLE

static BLEService doorService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
// write-only: this is a fire-and-forget command channel, and BLE_CheckCommand consumes
// the value, so reading it back would tell a client nothing
static BLEByteCharacteristic doorCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e", BLEWrite);
static BLEByteCharacteristic speedCharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite | BLENotify);
static BLEStringCharacteristic nameCharacteristic("6e400004-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite, MAX_NAME_LEN);
static BLEByteCharacteristic stateCharacteristic("6e400005-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLENotify);
static BLEByteCharacteristic reverseCharacteristic("6e400006-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite);

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

void reverseCharacteristicWrittenHandler(BLEDevice central, BLECharacteristic characteristic) {
    uint8_t reversed = reverseCharacteristic.value() ? 1 : 0;
    Serial.print("Reverse written : ");
    Serial.println(reversed);
    Storage_SetReversed(reversed);
    // echo back the normalised value, so the app never shows anything but 0 or 1
    reverseCharacteristic.writeValue(reversed);
}

void nameCharacteristicWrittenHandler(BLEDevice central, BLECharacteristic characteristic) {
    String newName = nameCharacteristic.value();
    Serial.println("Name written : " + newName);

    if (newName.length() == 0) {
        // refuse to make the board anonymous, and tell the app what the name still is
        nameCharacteristic.writeValue(Storage_GetName());
        return;
    }

    Storage_SetName(newName.c_str());
    // echo back what was actually stored, which may have been truncated
    nameCharacteristic.writeValue(Storage_GetName());
    BLE_UpdateName(Storage_GetName());
}

// Advertising data is only pushed to the controller by BLE.advertise(), so the new name
// takes effect from the next advertising cycle on. BLE.advertise() stops advertising
// before restarting it, so calling it again here is safe.
void BLE_UpdateName(const char* name) {
    BLE.setLocalName(name);
    BLE.advertise();
}

bool BLE_Init() {
    if (!BLE.begin()) {
        Serial.println("starting Bluetooth® Low Energy module failed!");
        return false;
    }

    // The name lives in flash, so each board can be told apart in the app. It goes into
    // the scan response rather than the advertising packet - setLocalName() writes to
    // _scanResponseData - which is what leaves room for MAX_NAME_LEN characters next to
    // the 128-bit service UUID.
    BLE.setLocalName(Storage_GetName());
    BLE.setAdvertisedService(doorService);
    doorService.addCharacteristic(doorCharacteristic);
    doorService.addCharacteristic(speedCharacteristic);
    doorService.addCharacteristic(nameCharacteristic);
    doorService.addCharacteristic(stateCharacteristic);
    doorService.addCharacteristic(reverseCharacteristic);
    BLE.addService(doorService);
    speedCharacteristic.writeValue(Storage_GetSpeed());
    nameCharacteristic.writeValue(Storage_GetName());
    stateCharacteristic.writeValue(DOOR_STATE_STARTUP);
    reverseCharacteristic.writeValue(Storage_GetReversed());
    BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
    speedCharacteristic.setEventHandler(BLEWritten, speedCharacteristicWrittenHandler);
    nameCharacteristic.setEventHandler(BLEWritten, nameCharacteristicWrittenHandler);
    reverseCharacteristic.setEventHandler(BLEWritten, reverseCharacteristicWrittenHandler);
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

void BLE_UpdateState(uint8_t state) {
    Serial.print("State notification sent: ");
    Serial.println(state);
    stateCharacteristic.writeValue(state);
}

#elif FEATHER_SENSE

static BLEService doorService = BLEService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic doorCharacteristic = BLECharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic speedCharacteristic = BLECharacteristic("6e400003-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic nameCharacteristic = BLECharacteristic("6e400004-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic stateCharacteristic = BLECharacteristic("6e400005-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic reverseCharacteristic = BLECharacteristic("6e400006-b5a3-f393-e0a9-e50e24dcca9e");

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

void reverseCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len != 1) {
        Serial.print("Reverse: unexpected length: ");
        Serial.println(len);
        return;
    }

    uint8_t reversed = data[0] ? 1 : 0;
    Serial.print("Reverse written : ");
    Serial.println(reversed);
    Storage_SetReversed(reversed);
    // echo back the normalised value, so the app never shows anything but 0 or 1
    reverseCharacteristic.write8(reversed);
}

void nameCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    char newName[MAX_NAME_LEN + 1];
    uint16_t copied = (len > MAX_NAME_LEN) ? MAX_NAME_LEN : len;

    memcpy(newName, data, copied);
    newName[copied] = '\0';
    Serial.print("Name written : ");
    Serial.println(newName);

    if (copied == 0) {
        // refuse to make the board anonymous
        return;
    }

    Storage_SetName(newName);
    nameCharacteristic.write(Storage_GetName(), strlen(Storage_GetName()));
    BLE_UpdateName(Storage_GetName());
}

// The name is baked into the advertising payload when it is built, so changing it means
// rebuilding that payload and restarting the advertising.
static void startAdvertising() {
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(doorService);
    // in the scan response, not the advertising packet: the 128-bit service UUID leaves
    // no room for a name of a useful length next to it
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
}

void BLE_UpdateName(const char* name) {
    Bluefruit.setName(name);
    startAdvertising();
}

bool BLE_Init() {
    Serial.println("BLE_Init: Starting Bluefruit...");
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName(Storage_GetName());
    Bluefruit.Periph.setConnectCallback(blePeripheralConnectHandler);
    Bluefruit.Periph.setDisconnectCallback(blePeripheralDisconnectHandler);
    
    Serial.println("BLE_Init: Starting door service...");
    doorService.begin();
    
    // write-only, see the ArduinoBLE branch
    doorCharacteristic.setProperties(CHR_PROPS_WRITE);
    doorCharacteristic.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
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
    
    // Configuration de la caractéristique de nom
    nameCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    nameCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    nameCharacteristic.setMaxLen(MAX_NAME_LEN);
    nameCharacteristic.setWriteCallback(nameCharacteristicWrittenHandler);
    nameCharacteristic.begin();
    nameCharacteristic.write(Storage_GetName(), strlen(Storage_GetName()));
    Serial.println("BLE_Init: Name characteristic configured");

    // Configuration de la caractéristique d'état
    stateCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    stateCharacteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    stateCharacteristic.setFixedLen(1);
    stateCharacteristic.begin();
    stateCharacteristic.write8(DOOR_STATE_STARTUP);
    Serial.println("BLE_Init: State characteristic configured");

    // Configuration de la caractéristique de sens moteur
    reverseCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE);
    reverseCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    reverseCharacteristic.setFixedLen(1);
    reverseCharacteristic.setWriteCallback(reverseCharacteristicWrittenHandler);
    reverseCharacteristic.begin();
    reverseCharacteristic.write8(Storage_GetReversed());
    Serial.println("BLE_Init: Reverse characteristic configured");

    startAdvertising();

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

void BLE_UpdateState(uint8_t state) {
    // keep the readable value up to date even with no subscriber; notify8 simply
    // returns false when nobody is listening
    stateCharacteristic.write8(state);
    bool success = stateCharacteristic.notify8(state);

    Serial.print("State notification sent: ");
    Serial.print(state);
    Serial.print(" - ");
    Serial.println(success ? "SUCCESS" : "FAILED");
}

#endif

#endif // BLE_HAL_H