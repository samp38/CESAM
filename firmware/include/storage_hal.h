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
  uint8_t speed;  // ← uint8_t au lieu de int (1 byte explicite)
} flashPrefs;

// Public API
void Storage_Init();
bool Storage_ReadPrefs();
bool Storage_WritePrefs();
void Storage_SetDefaults();
uint8_t Storage_GetSpeed();
void Storage_SetSpeed(uint8_t speed);
void Storage_PrintPrefs();

// Implementation
#ifdef NANO_33_BLE

static NanoBLEFlashPrefs myFlashPrefs;
static flashPrefs globalPrefs;

void Storage_Init() {
    // Lancer un garbage collection au démarrage
    Serial.println("Running flash garbage collection...");
    myFlashPrefs.garbageCollection();
    delay(100);
}

void Storage_PrintPrefs() {
    Serial.println("Preferences: ");
    Serial.println(globalPrefs.pref_doorName);
    Serial.println(globalPrefs.speed);
}

bool Storage_ReadPrefs() {
    int rc = myFlashPrefs.readPrefs(&globalPrefs, sizeof(globalPrefs));
    if (rc == FDS_SUCCESS) {
        Serial.println("Storage read SUCCESS");
        Storage_PrintPrefs();
        return true;
    }
    Serial.print("Storage read FAILED, code: ");
    Serial.println(rc);
    return false;
}

bool Storage_WritePrefs() {
    Serial.print("Writing prefs... speed=");
    Serial.println(globalPrefs.speed);
    
    int rc = myFlashPrefs.writePrefs(&globalPrefs, sizeof(globalPrefs));
    
    if (rc == FDS_SUCCESS) {
        Serial.println("Storage write SUCCESS");
        return true;
    } else if (rc == FDS_ERR_NO_SPACE_IN_FLASH) {
        Serial.println("Flash full! Running garbage collection...");
        myFlashPrefs.garbageCollection();
        delay(200);
        
        // Réessayer après GC
        rc = myFlashPrefs.writePrefs(&globalPrefs, sizeof(globalPrefs));
        if (rc == FDS_SUCCESS) {
            Serial.println("Storage write SUCCESS after GC");
            return true;
        }
    }
    
    Serial.print("Storage write FAILED, code: ");
    Serial.println(rc);
    return false;
}

void Storage_SetDefaults() {
    globalPrefs.speed = DEFAULT_SPEED;
    strcpy(globalPrefs.pref_doorName, DEFAULT_NAME);
}

uint8_t Storage_GetSpeed() {
    return globalPrefs.speed;
}

void Storage_SetSpeed(uint8_t speed) {
    Serial.print("Storage_SetSpeed called with: ");
    Serial.println(speed);
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
        Serial.println("Prefs file does not exist");
        return false;
    }
    
    file.open(PREFS_FILENAME, FILE_O_READ);
    if (file) {
        uint32_t readlen = file.read(&globalPrefs, sizeof(globalPrefs));
        file.close();
        if (readlen == sizeof(globalPrefs)) {
            Serial.println("Storage read SUCCESS");
            Storage_PrintPrefs();
            return true;
        }
    }
    Serial.println("Storage read FAILED");
    return false;
}

bool Storage_WritePrefs() {
    Serial.print("Writing prefs... speed=");
    Serial.println(globalPrefs.speed);
    
    InternalFS.remove(PREFS_FILENAME);
    file.open(PREFS_FILENAME, FILE_O_WRITE);
    if (file) {
        file.write((uint8_t*)&globalPrefs, sizeof(globalPrefs));
        file.close();
        Serial.println("Storage write SUCCESS");
        return true;
    }
    Serial.println("Storage write FAILED");
    return false;
}

void Storage_SetDefaults() {
    globalPrefs.speed = DEFAULT_SPEED;
    strcpy(globalPrefs.pref_doorName, DEFAULT_NAME);
}

uint8_t Storage_GetSpeed() {
    return globalPrefs.speed;
}

void Storage_SetSpeed(uint8_t speed) {
    Serial.print("Storage_SetSpeed called with: ");
    Serial.println(speed);
    globalPrefs.speed = speed;
}

#endif

#endif // STORAGE_HAL_H