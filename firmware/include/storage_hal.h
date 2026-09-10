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
#define DEFAULT_NAME_PREFIX "CESAM"

// The name is advertised in the scan response, whose payload is 31 bytes: one length
// byte and one type byte precede it, so 29 characters are all that fit.
#define MAX_NAME_LEN 29

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
const char* Storage_GetName();
void Storage_SetName(const char* name);
void Storage_Process();
void Storage_PrintPrefs();

// Both supported boards are nRF52840, so this is board-independent.
//
// A board with nothing in flash yet names itself after its factory device ID, so that
// several fresh boards can be told apart in the app before anyone renames them. The BLE
// address would read better but is not available yet: defaults are applied during
// startup, before BLE_Init().
static void Storage_DefaultName(char* out, size_t size) {
    snprintf(out, size, "%s-%08lX", DEFAULT_NAME_PREFIX,
             (unsigned long) NRF_FICR->DEVICEID[1]);
}

// Run on whatever came back from flash. A record written by a firmware that never set a
// name, or a truncated read, would otherwise leave the board advertising an empty or
// unterminated string.
static void Storage_EnsureName(char* name, size_t size) {
    name[size - 1] = '\0';

    if (name[0] == '\0') {
        Storage_DefaultName(name, size);
    }
}

// Implementation
#ifdef NANO_33_BLE

static NanoBLEFlashPrefs myFlashPrefs;
static flashPrefs globalPrefs;
static unsigned long last_write_time = 0;
static uint8_t pending_speed = 0;
static bool write_pending = false;

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
        Storage_EnsureName(globalPrefs.pref_doorName, sizeof(globalPrefs.pref_doorName));
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
    Storage_DefaultName(globalPrefs.pref_doorName, sizeof(globalPrefs.pref_doorName));
}

uint8_t Storage_GetSpeed() {
    return globalPrefs.speed;
}

void Storage_SetSpeed(uint8_t speed) {
    Serial.print("Storage_SetSpeed called with: ");
    Serial.println(speed);
    globalPrefs.speed = speed;
    pending_speed = speed;
    write_pending = true;
    last_write_time = millis();
}

const char* Storage_GetName() {
    return globalPrefs.pref_doorName;
}

void Storage_SetName(const char* name) {
    Serial.print("Storage_SetName called with: ");
    Serial.println(name);
    strncpy(globalPrefs.pref_doorName, name, MAX_NAME_LEN);
    globalPrefs.pref_doorName[MAX_NAME_LEN] = '\0';
    write_pending = true;
    last_write_time = millis();
}

void Storage_Process() {
    if (write_pending && (millis() - last_write_time > 3000)) {
        // Écrire seulement si pas de changement depuis 2s
        write_pending = false;
        Storage_WritePrefs();
    }
}

#elif FEATHER_SENSE

using namespace Adafruit_LittleFS_Namespace;

#define PREFS_FILENAME "/prefs.dat"
static File file(InternalFS);
static flashPrefs globalPrefs;
static unsigned long last_write_time = 0;
static bool write_pending = false;

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
            Storage_EnsureName(globalPrefs.pref_doorName, sizeof(globalPrefs.pref_doorName));
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
    Storage_DefaultName(globalPrefs.pref_doorName, sizeof(globalPrefs.pref_doorName));
}

uint8_t Storage_GetSpeed() {
    return globalPrefs.speed;
}

void Storage_SetSpeed(uint8_t speed) {
    Serial.print("Storage_SetSpeed called with: ");
    Serial.println(speed);
    globalPrefs.speed = speed;
    write_pending = true;
    last_write_time = millis();
}

const char* Storage_GetName() {
    return globalPrefs.pref_doorName;
}

void Storage_SetName(const char* name) {
    Serial.print("Storage_SetName called with: ");
    Serial.println(name);
    strncpy(globalPrefs.pref_doorName, name, MAX_NAME_LEN);
    globalPrefs.pref_doorName[MAX_NAME_LEN] = '\0';
    write_pending = true;
    last_write_time = millis();
}

void Storage_Process() {
    if (write_pending && (millis() - last_write_time > 3000)) {
        // Écrire seulement si pas de changement depuis 2s
        write_pending = false;
        Storage_WritePrefs();
    }
}

#endif

#endif // STORAGE_HAL_H