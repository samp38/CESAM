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
#define DOOR_STATE_TIMEOUT 7  // travel cut short by the safety cap, position unknown

// Every persistent setting travels together on characteristic ...0007, as one big-endian
// block. One characteristic rather than one per setting: adding a parameter then costs a
// field and two lines of coding, instead of a characteristic, a write handler, a storage
// pair and a read on the app side - in both board branches.
//
//   [0]   speed            1-255
//   [1]   reversed         0 or 1
//   [2:3] stopTimeoutMs    milliseconds
//   [4:5] moveThreshold    tenths of a degree per second
#define BLE_SETTINGS_LEN 6

// Public API
bool BLE_Init();
uint8_t BLE_CheckCommand();
void BLE_UpdateSettings();
void BLE_UpdateName(const char* name);
void BLE_UpdateState(uint8_t state);

static void BLE_EncodeSettings(uint8_t out[BLE_SETTINGS_LEN]) {
    uint16_t timeout = Storage_GetStopTimeoutMs();
    uint16_t threshold = Storage_GetMoveThreshold();

    out[0] = Storage_GetSpeed();
    out[1] = Storage_GetReversed();
    out[2] = (timeout >> 8) & 0xFF;
    out[3] = timeout & 0xFF;
    out[4] = (threshold >> 8) & 0xFF;
    out[5] = threshold & 0xFF;
}

// The whole block is written at once, so a partial write is refused rather than applied
// half way. Each setter clamps, which is why the board echoes the block back afterwards.
static bool BLE_ApplySettings(const uint8_t* data, int len) {
    if (len != BLE_SETTINGS_LEN) {
        Serial.print("Settings: unexpected length: ");
        Serial.println(len);
        return false;
    }

    Storage_SetSpeed(data[0]);
    Storage_SetReversed(data[1]);
    Storage_SetStopTimeoutMs((uint16_t)((data[2] << 8) | data[3]));
    Storage_SetMoveThreshold((uint16_t)((data[4] << 8) | data[5]));
    return true;
}

// Implementation
#ifdef NANO_33_BLE

static BLEService doorService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
// write-only: this is a fire-and-forget command channel, and BLE_CheckCommand consumes
// the value, so reading it back would tell a client nothing
static BLEByteCharacteristic doorCharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e", BLEWrite);
static BLEStringCharacteristic nameCharacteristic("6e400004-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite, MAX_NAME_LEN);
static BLEByteCharacteristic stateCharacteristic("6e400005-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLENotify);
static BLECharacteristic settingsCharacteristic("6e400007-b5a3-f393-e0a9-e50e24dcca9e", BLERead | BLEWrite | BLENotify, BLE_SETTINGS_LEN, true);

void blePeripheralConnectHandler(BLEDevice central) {
    Serial.print("Connected event, central: ");
    Serial.println(central.address());
}

void settingsCharacteristicWrittenHandler(BLEDevice central, BLECharacteristic characteristic) {
    Serial.println("Settings written");

    if (BLE_ApplySettings(settingsCharacteristic.value(),
                          settingsCharacteristic.valueLength())) {
        // echo the stored block back: the setters clamp, so what the app sent and what
        // took effect are not necessarily the same
        BLE_UpdateSettings();
    }
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
    doorService.addCharacteristic(nameCharacteristic);
    doorService.addCharacteristic(stateCharacteristic);
    doorService.addCharacteristic(settingsCharacteristic);
    BLE.addService(doorService);
    nameCharacteristic.writeValue(Storage_GetName());
    stateCharacteristic.writeValue(DOOR_STATE_STARTUP);
    BLE_UpdateSettings();
    BLE.setEventHandler(BLEConnected, blePeripheralConnectHandler);
    nameCharacteristic.setEventHandler(BLEWritten, nameCharacteristicWrittenHandler);
    settingsCharacteristic.setEventHandler(BLEWritten, settingsCharacteristicWrittenHandler);
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

void BLE_UpdateSettings() {
    uint8_t block[BLE_SETTINGS_LEN];

    BLE_EncodeSettings(block);
    settingsCharacteristic.writeValue(block, BLE_SETTINGS_LEN);
}

void BLE_UpdateState(uint8_t state) {
    Serial.print("State notification sent: ");
    Serial.println(state);
    stateCharacteristic.writeValue(state);
}

#elif FEATHER_SENSE

static BLEService doorService = BLEService("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic doorCharacteristic = BLECharacteristic("6e400002-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic settingsCharacteristic = BLECharacteristic("6e400007-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic nameCharacteristic = BLECharacteristic("6e400004-b5a3-f393-e0a9-e50e24dcca9e");
static BLECharacteristic stateCharacteristic = BLECharacteristic("6e400005-b5a3-f393-e0a9-e50e24dcca9e");

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

void settingsCccdCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint16_t value) {
    Serial.print("Settings CCCD updated: ");
    Serial.println(value);

    if (value & 0x0001) {
        Serial.println("Client SUBSCRIBED to notifications");
        clientSubscribed = true;
        // Envoyer immédiatement les réglages quand le client s'abonne
        delay(100);
        BLE_UpdateSettings();
    } else {
        Serial.println("Client UNSUBSCRIBED from notifications");
        clientSubscribed = false;
    }
}

void settingsCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    Serial.print("Settings written, length: ");
    Serial.println(len);

    if (BLE_ApplySettings(data, len)) {
        // echo the stored block back: the setters clamp, so what the app sent and what
        // took effect are not necessarily the same
        BLE_UpdateSettings();
    }
}

void doorCharacteristicWrittenHandler(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len == 1) {
        lastDoorCommand = data[0];
        Serial.print("Door command received: ");
        Serial.println((char)lastDoorCommand);
    }
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
    
    // Configuration de la caractéristique de réglages avec notification
    settingsCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
    settingsCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    settingsCharacteristic.setFixedLen(BLE_SETTINGS_LEN);
    settingsCharacteristic.setWriteCallback(settingsCharacteristicWrittenHandler);
    settingsCharacteristic.setCccdWriteCallback(settingsCccdCallback);  // ← IMPORTANT !
    settingsCharacteristic.begin();
    BLE_UpdateSettings();
    Serial.println("BLE_Init: Settings characteristic configured");

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

void BLE_UpdateSettings() {
    uint8_t block[BLE_SETTINGS_LEN];

    BLE_EncodeSettings(block);
    // keep the readable value up to date even with no subscriber
    settingsCharacteristic.write(block, BLE_SETTINGS_LEN);

    if (!clientSubscribed) {
        Serial.println("BLE_UpdateSettings: Client not subscribed, skipping notification");
        return;
    }

    bool success = settingsCharacteristic.notify(block, BLE_SETTINGS_LEN);

    Serial.print("Settings notification sent - ");
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