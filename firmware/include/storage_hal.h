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
#define DEFAULT_REVERSED 0

// How long the door must show no rotation before the travel is called finished, and how
// much rotation counts as movement, in tenths of a degree per second. Both are settable
// from the app, and both are clamped: a threshold near zero makes noise look like
// movement and the travel would never end on its own. MAX_TRAVEL_MS in main.cpp is the
// backstop if they are set to a combination that still never triggers.
#define DEFAULT_STOP_TIMEOUT_MS 1000
#define MIN_STOP_TIMEOUT_MS      200
#define MAX_STOP_TIMEOUT_MS     5000

#define DEFAULT_MOVE_THRESHOLD 60   // 6.0 deg/s, the value VZ_TH_MOVE used to hold
#define MIN_MOVE_THRESHOLD     10   // 1.0 deg/s, below which gyro noise dominates
// 50.0 deg/s. A door swinging 90 degrees in three seconds turns at about 30 deg/s, so
// anything above this never sees the door move at all and the travel would just end
// after stopTimeoutMs every time.
#define MAX_MOVE_THRESHOLD     500

// The name is advertised in the scan response, whose payload is 31 bytes: one length
// byte and one type byte precede it, so 29 characters are all that fit.
#define MAX_NAME_LEN 29

// Adding a field here changes sizeof(flashPrefs), which makes the stored record
// unreadable: the next boot falls back to Storage_SetDefaults(), so speed goes back to
// 255 and the name is regenerated. A one-off reset per firmware upgrade, not a data loss
// worth migrating around at this stage.
typedef struct {
  char pref_doorName[64];
  uint8_t speed;  // ← uint8_t au lieu de int (1 byte explicite)
  uint8_t reversed;  // motor wired the other way round: swap the two directions
  uint16_t stopTimeoutMs;
  uint16_t moveThreshold;  // tenths of a degree per second
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
uint8_t Storage_GetReversed();
void Storage_SetReversed(uint8_t reversed);
uint16_t Storage_GetStopTimeoutMs();
void Storage_SetStopTimeoutMs(uint16_t ms);
uint16_t Storage_GetMoveThreshold();
void Storage_SetMoveThreshold(uint16_t tenths);
void Storage_Process();
void Storage_PrintPrefs();

// Keeps a value the app sent inside the range the firmware can work with. The clamped
// value is what gets stored and echoed back, so the app shows what actually took effect.
static uint16_t Storage_Clamp(uint16_t value, uint16_t low, uint16_t high) {
    if (value < low)  { return low; }
    if (value > high) { return high; }
    return value;
}

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

// Applied to whatever came back from flash, before anything uses it. A record written by
// an older firmware, or a partial read, would otherwise put out-of-range values straight
// into the stop detection.
static void Storage_EnsurePrefs(flashPrefs* prefs) {
    Storage_EnsureName(prefs->pref_doorName, sizeof(prefs->pref_doorName));
    prefs->stopTimeoutMs = Storage_Clamp(prefs->stopTimeoutMs,
                                         MIN_STOP_TIMEOUT_MS, MAX_STOP_TIMEOUT_MS);
    prefs->moveThreshold = Storage_Clamp(prefs->moveThreshold,
                                         MIN_MOVE_THRESHOLD, MAX_MOVE_THRESHOLD);
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
    Serial.println(globalPrefs.reversed);
}

bool Storage_ReadPrefs() {
    int rc = myFlashPrefs.readPrefs(&globalPrefs, sizeof(globalPrefs));
    if (rc == FDS_SUCCESS) {
        Serial.println("Storage read SUCCESS");
        Storage_EnsurePrefs(&globalPrefs);
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
    globalPrefs.reversed = DEFAULT_REVERSED;
    globalPrefs.stopTimeoutMs = DEFAULT_STOP_TIMEOUT_MS;
    globalPrefs.moveThreshold = DEFAULT_MOVE_THRESHOLD;
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

uint8_t Storage_GetReversed() {
    return globalPrefs.reversed;
}

void Storage_SetReversed(uint8_t reversed) {
    Serial.print("Storage_SetReversed called with: ");
    Serial.println(reversed);
    globalPrefs.reversed = reversed ? 1 : 0;
    write_pending = true;
    last_write_time = millis();
}

uint16_t Storage_GetStopTimeoutMs() {
    return globalPrefs.stopTimeoutMs;
}

void Storage_SetStopTimeoutMs(uint16_t ms) {
    globalPrefs.stopTimeoutMs = Storage_Clamp(ms, MIN_STOP_TIMEOUT_MS, MAX_STOP_TIMEOUT_MS);
    Serial.print("Storage_SetStopTimeoutMs stored: ");
    Serial.println(globalPrefs.stopTimeoutMs);
    write_pending = true;
    last_write_time = millis();
}

uint16_t Storage_GetMoveThreshold() {
    return globalPrefs.moveThreshold;
}

void Storage_SetMoveThreshold(uint16_t tenths) {
    globalPrefs.moveThreshold = Storage_Clamp(tenths, MIN_MOVE_THRESHOLD, MAX_MOVE_THRESHOLD);
    Serial.print("Storage_SetMoveThreshold stored: ");
    Serial.println(globalPrefs.moveThreshold);
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
    Serial.println(globalPrefs.reversed);
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
            Storage_EnsurePrefs(&globalPrefs);
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
    globalPrefs.reversed = DEFAULT_REVERSED;
    globalPrefs.stopTimeoutMs = DEFAULT_STOP_TIMEOUT_MS;
    globalPrefs.moveThreshold = DEFAULT_MOVE_THRESHOLD;
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

uint8_t Storage_GetReversed() {
    return globalPrefs.reversed;
}

void Storage_SetReversed(uint8_t reversed) {
    Serial.print("Storage_SetReversed called with: ");
    Serial.println(reversed);
    globalPrefs.reversed = reversed ? 1 : 0;
    write_pending = true;
    last_write_time = millis();
}

uint16_t Storage_GetStopTimeoutMs() {
    return globalPrefs.stopTimeoutMs;
}

void Storage_SetStopTimeoutMs(uint16_t ms) {
    globalPrefs.stopTimeoutMs = Storage_Clamp(ms, MIN_STOP_TIMEOUT_MS, MAX_STOP_TIMEOUT_MS);
    Serial.print("Storage_SetStopTimeoutMs stored: ");
    Serial.println(globalPrefs.stopTimeoutMs);
    write_pending = true;
    last_write_time = millis();
}

uint16_t Storage_GetMoveThreshold() {
    return globalPrefs.moveThreshold;
}

void Storage_SetMoveThreshold(uint16_t tenths) {
    globalPrefs.moveThreshold = Storage_Clamp(tenths, MIN_MOVE_THRESHOLD, MAX_MOVE_THRESHOLD);
    Serial.print("Storage_SetMoveThreshold stored: ");
    Serial.println(globalPrefs.moveThreshold);
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