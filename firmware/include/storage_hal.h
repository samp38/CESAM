// storage_hal.h
#ifndef STORAGE_HAL_H
#define STORAGE_HAL_H

#include <Arduino.h>

#ifdef NANO_33_BLE
#include <NanoBLEFlashPrefs.h>
#elif FEATHER_SENSE
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#endif

#define DEFAULT_SPEED 255
#define DEFAULT_NAME "CESAM_DOOR"

typedef struct {
  char pref_doorName[64];
  int speed;
} flashPrefs;

// Public API
void Storage_Init();
bool Storage_ReadPrefs();
bool Storage_WritePrefs();
void Storage_SetDefaults();
int Storage_GetSpeed();
void Storage_SetSpeed(int speed);
void Storage_PrintPrefs();

// Implementation
#ifdef NANO_33_BLE

static NanoBLEFlashPrefs myFlashPrefs;
static flashPrefs globalPrefs;

void Storage_Init() {
    // Nothing specific to initialize for NanoBLE
}

void Storage_PrintPrefs() {
    Serial.println("Preferences: ");
    Serial.println(globalPrefs.pref_doorName);
    Serial.println(globalPrefs.speed);
}

bool Storage_ReadPrefs() {
    int rc = myFlashPrefs.readPrefs(&globalPrefs, sizeof(globalPrefs));
    if (rc == FDS_SUCCESS) {
        Storage_PrintPrefs();
        return true;
    }
    return false;
}

bool Storage_WritePrefs() {
    myFlashPrefs.writePrefs(&globalPrefs, sizeof(globalPrefs));
    return true;
}

void Storage_SetDefaults() {
    globalPrefs.speed = DEFAULT_SPEED;
    strcpy(globalPrefs.pref_doorName, DEFAULT_NAME);
}

int Storage_GetSpeed() {
    return globalPrefs.speed;
}

void Storage_SetSpeed(int speed) {
    globalPrefs.speed = speed;
}

#elif FEATHER_SENSE

using namespace Adafruit_LittleFS_Namespace;

#define PREFS_FILENAME "/prefs.dat"
static File file(InternalFS);
static flashPrefs globalPrefs;

void Storage_Init() {
    InternalFS.begin();
}

void Storage_PrintPrefs() {
    Serial.println("Preferences: ");
    Serial.println(globalPrefs.pref_doorName);
    Serial.println(globalPrefs.speed);
}

bool Storage_ReadPrefs() {
    if (!InternalFS.exists(PREFS_FILENAME)) {
        return false;
    }
    
    file.open(PREFS_FILENAME, FILE_O_READ);
    if (file) {
        uint32_t readlen = file.read(&globalPrefs, sizeof(globalPrefs));
        file.close();
        if (readlen == sizeof(globalPrefs)) {
            Storage_PrintPrefs();
            return true;
        }
    }
    return false;
}

bool Storage_WritePrefs() {
    InternalFS.remove(PREFS_FILENAME);
    file.open(PREFS_FILENAME, FILE_O_WRITE);
    if (file) {
        file.write((uint8_t*)&globalPrefs, sizeof(globalPrefs));
        file.close();
        return true;
    }
    return false;
}

void Storage_SetDefaults() {
    globalPrefs.speed = DEFAULT_SPEED;
    strcpy(globalPrefs.pref_doorName, DEFAULT_NAME);
}

int Storage_GetSpeed() {
    return globalPrefs.speed;
}

void Storage_SetSpeed(int speed) {
    globalPrefs.speed = speed;
}

#endif

#endif // STORAGE_HAL_H