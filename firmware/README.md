# State Machine - CESAM Door Opener

## Overview

The CESAM system uses a state machine to manage the motorized opening/closing of a door. End-of-travel detection is performed via an IMU gyroscope that measures the door's rotational movement.

## State Diagram

```
┌─────────────┐
│   Startup   │
└──────┬──────┘
       │ Init IMU, BLE, Storage
       ▼
┌─────────────┐
│   Unknown   │
└──────┬──────┘
       │
       │              ┌─────────────┐
       ├─────────────►│   Opening   │
       │ BLE: '0'     └──────┬──────┘
       │                     │ Timeout (1s no movement)
       │                     ▼
       │              ┌─────────────┐
       │              │    Open     │
       │              └──────┬──────┘
       │                     │ BLE: '1'
       │                     ▼
       │              ┌─────────────┐
       └─────────────►│   Closing   │
         BLE: '1'     └──────┬──────┘
                             │ Timeout (1s no movement)
                             ▼
                      ┌─────────────┐
                      │   Closed    │
                      └─────────────┘

BLE: '3' from Opening or Closing ────► Paused
```

`Unknown`, `Open`, `Closed` and `Paused` are the door at rest. They share one class,
`RestingState`, instantiated four times: what differs is only the value reported to the
app, and the state the machine transitions to *is* the reason the door stopped, so there
is no separate field to keep in sync. Any of them accepts `'0'` and `'1'` to start a new
travel.

## States

### 1. StartupState

**Role**: System initialization at boot

**Actions**:
- Initialize IMU with automatic gyroscope calibration
- Load preferences from flash (motor speed)
- Initialize BLE, advertising the name stored in flash (default: "CESAM_DOOR")
- Stop motor for safety

**Transition**: → `RestingState(UNKNOWN)` automatically after init

---

### 2. RestingState

**Role**: Door at rest, motor stopped, waiting for BLE commands

Instantiated four times, one per reason the door came to rest. The instance is what the
app is told on the state characteristic:

| Instance | Reported | Reached from |
|----------|----------|--------------|
| `unknownState` | `DOOR_STATE_UNKNOWN` (1) | end of startup, no travel completed yet |
| `openState` | `DOOR_STATE_OPEN` (2) | `OpeningState` ran to completion |
| `closedState` | `DOOR_STATE_CLOSED` (3) | `ClosingState` ran to completion |
| `pausedState` | `DOOR_STATE_PAUSED` (4) | BLE command '3' during a travel |

**Actions**:
- Motor stopped
- Listening for BLE commands

**Transitions**:
- BLE command '0' → `OpeningState`
- BLE command '1' → `ClosingState`
- BLE command '2' → Refresh (stays put)
- BLE command '3' → `pausedState`

> `OPEN` and `CLOSED` report that a travel ran to completion, **not** a measured
> position: there is no limit switch, the end of travel is inferred from the IMU no
> longer seeing rotation. A door jammed mid-travel therefore also reports `OPEN` or
> `CLOSED`. Distinguishing the two needs motor current measurement — see
> [Future Improvements](#future-improvements).

---

### 3. OpeningState

**Role**: Door opening in progress

**Entry actions**:
- Start motor in opening direction
- Initialize movement detection timer

**During execution**:
- Read gyroscope (100ms period)
- Calculate total rotation: `|x| + |y| + |z|`
- If rotation > `VZ_TH_MOVE` (6 deg/s) → reset timer
- If no movement for > 1 second → end-of-travel detected

**Exit actions**:
- Stop motor

**Transitions**:
- 1s timeout without movement → `openState`
- BLE command '1' → `ClosingState` (manual reversal)
- BLE command '3' → `pausedState` (emergency stop)

---

### 4. ClosingState

**Role**: Door closing in progress

**Actions**: Identical to `OpeningState` but reversed rotation direction

**Transitions**:
- 1s timeout without movement → `closedState`
- BLE command '0' → `OpeningState` (manual reversal)
- BLE command '3' → `pausedState` (emergency stop)

---

## Configuration Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `VZ_TH` | 2.0 deg/s | Manual movement detection threshold (currently unused) |
| `VZ_TH_MOVE` | 6.0 deg/s | Motor movement detection threshold |
| `LOOP_TIME_MS` | 100 ms | State processing period |
| **No-movement timeout** | 1000 ms | Delay before stop if no movement detected |
| **Motor startup delay** | 500 ms | Wait time after motor start |

---

## End-of-Travel Detection

### Principle

Since the door rotates, the gyroscope detects angular movement. When the door reaches its end stop:
1. Rotation stops abruptly
2. Gyroscope drops below `VZ_TH_MOVE` threshold
3. After 1 second without movement → end-of-travel confirmed
4. Motor automatically stopped

### Total Rotation Calculation

```cpp
float total_rot = abs(x) + abs(y) + abs(z);
```

We use the sum of absolute values from all 3 axes to detect any rotation, regardless of module orientation.

---

## BLE Commands

**Service UUID**: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`

| Characteristic | UUID suffix | Properties | Payload |
|----------------|-------------|------------|---------|
| Commands | `...0002` | Write | 1 byte, **ASCII** |
| Speed | `...0003` | Read, Write, Notify | 1 byte, raw |
| Name | `...0004` | Read, Write | up to 29 chars |
| State | `...0005` | Read, Notify | 1 byte, raw enum |

Note the asymmetry: commands are ASCII characters, everything else is raw bytes. The
commands are kept in ASCII because they read directly in the serial logs.

### Commands (`...0002`)

| Command | ASCII Code | Action |
|---------|------------|--------|
| Open | `'0'` (48) | Start opening |
| Close | `'1'` (49) | Start closing |
| Refresh | `'2'` (50) | Push current motor speed **and** current state |
| Pause | `'3'` (51) | Stop the motor where it is → `pausedState` |

### State (`...0005`)

Notified from the single transition point of the state machine, so every change is
reported — including the ones the app did not ask for, such as an end of travel. Since a
board may sit in the same state for a long time, the app has no way of learning the
current state right after connecting: that is what `'2'` is for.

| Value | Name | Meaning |
|-------|------|---------|
| 0 | `DOOR_STATE_STARTUP` | initialising |
| 1 | `DOOR_STATE_UNKNOWN` | at rest, no travel completed since boot |
| 2 | `DOOR_STATE_OPEN` | opening travel ran to completion |
| 3 | `DOOR_STATE_CLOSED` | closing travel ran to completion |
| 4 | `DOOR_STATE_PAUSED` | stopped part-way by a pause command |
| 5 | `DOOR_STATE_OPENING` | opening in progress |
| 6 | `DOOR_STATE_CLOSING` | closing in progress |

### Name (`...0004`)

The door name lives in flash (`pref_doorName`) and is advertised in the **scan
response** — not in the advertising packet, which the 128-bit service UUID nearly fills.
That caps it at `MAX_NAME_LEN` = 29 characters. Writing this characteristic renames the
board and restarts advertising; the board echoes back what it actually stored, which may
have been truncated. This is what lets the app tell several boards apart.

---

## Safety Features

### 1. Blockage Protection
If the door is blocked (obstacle, end stop), the gyroscope no longer detects movement → stop after 1s

### 2. Emergency Stop
A BLE command can stop the motor at any time by changing state

### 3. No Unintended Movement
The system requires an explicit BLE command to start, it never activates automatically

---

## Gyroscope Calibration

At startup (in `IMU_Init()`), the system performs automatic calibration:

1. **1 second wait** - Message displayed: "KEEP THE BOARD STILL"
2. **200 samples** averaged over 2 seconds
3. **Offset calculation** to compensate gyroscope bias
4. **Offset application** at each reading

**Important**: The board must be **stationary** during calibration!

---

## Future Improvements

### Architecture
- [ ] Encapsulate state machine in a class
- [ ] Separate business logic into dedicated file

### End-of-Travel Detection
- [ ] Adaptive timeout based on progressive deceleration vs abrupt stop
- [ ] Wheel slip detection (high-frequency oscillations)
- [ ] Motor current measurement to detect overload

### Features
- [ ] Manual mode with automatic unlocking on detected movement
- [ ] Save position (open/closed) to flash
- [ ] Usage statistics (cycle count, average duration)
- [ ] Threshold configuration via BLE

---

## Code Architecture

```
firmware/
├── src/
│   └── main.cpp              # State machine + setup/loop
├── include/
│   ├── bsp.h                 # Pin definitions per board
│   ├── imu_hal.h             # IMU abstraction (BMI270/LSM6DS33)
│   ├── ble_hal.h             # BLE abstraction (ArduinoBLE/Bluefruit)
│   ├── storage_hal.h         # Flash abstraction (NanoBLEFlashPrefs/LittleFS)
│   └── motor_control.h       # H-bridge motor control
└── platformio.ini            # Multi-board configuration
```

---

## Supported Boards

| Board | MCU | IMU | BLE | Flash |
|-------|-----|-----|-----|-------|
| **Arduino Nano 33 BLE** | nRF52840 | BMI270 | ArduinoBLE | NanoBLEFlashPrefs |
| **Adafruit Feather nRF52840 Sense** | nRF52840 | LSM6DS33 | Bluefruit | LittleFS |

Both boards are 100% compatible thanks to HAL (Hardware Abstraction Layers).

---

## License

MIT License - CESAM project for door accessibility for wheelchair users ♿