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
       │                     │ No movement for stopTimeoutMs
       │                     ▼
       │              ┌─────────────┐
       │              │    Open     │
       │              └──────┬──────┘
       │                     │ BLE: '1'
       │                     ▼
       │              ┌─────────────┐
       └─────────────►│   Closing   │
         BLE: '1'     └──────┬──────┘
                             │ No movement for stopTimeoutMs
                             ▼
                      ┌─────────────┐
                      │   Closed    │
                      └─────────────┘

BLE: '3' from Opening or Closing ─────────────► Paused
More than MAX_TRAVEL_MS in Opening or Closing ► Timeout
```

`Unknown`, `Open`, `Closed`, `Paused` and `Timeout` are the door at rest. They share one
class, `RestingState`, instantiated five times: what differs is only the value reported
to the app, and the state the machine transitions to *is* the reason the door stopped, so
there is no separate field to keep in sync. Any of them accepts `'0'` and `'1'` to start
a new travel.

## States

### 1. StartupState

**Role**: System initialization at boot

**Actions**:
- Initialize IMU with automatic gyroscope calibration
- Load settings from flash (name, speed, motor direction, stop detection)
- Initialize BLE, advertising the name stored in flash (default: "CESAM_DOOR")
- Stop motor for safety

**Transition**: → `RestingState(UNKNOWN)` automatically after init

---

### 2. RestingState

**Role**: Door at rest, motor stopped, waiting for BLE commands

Instantiated five times, one per reason the door came to rest. The instance is what the
app is told on the state characteristic:

| Instance | Reported | Reached from |
|----------|----------|--------------|
| `unknownState` | `DOOR_STATE_UNKNOWN` (1) | end of startup, no travel completed yet |
| `openState` | `DOOR_STATE_OPEN` (2) | `OpeningState` ran to completion |
| `closedState` | `DOOR_STATE_CLOSED` (3) | `ClosingState` ran to completion |
| `pausedState` | `DOOR_STATE_PAUSED` (4) | BLE command '3' during a travel |
| `timeoutState` | `DOOR_STATE_TIMEOUT` (7) | travel ran past `MAX_TRAVEL_MS` |

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
- If rotation > `moveThreshold` (6.0 deg/s by default) → reset timer
- If no movement for > `stopTimeoutMs` → end-of-travel detected

**Exit actions**:
- Stop motor

**Transitions**:
- No movement for `stopTimeoutMs` → `openState`
- More than `MAX_TRAVEL_MS` since the travel began → `timeoutState`
- BLE command '1' → `ClosingState` (manual reversal)
- BLE command '3' → `pausedState` (emergency stop)

---

### 4. ClosingState

**Role**: Door closing in progress

**Actions**: Identical to `OpeningState` but reversed rotation direction

**Transitions**:
- No movement for `stopTimeoutMs` → `closedState`
- More than `MAX_TRAVEL_MS` since the travel began → `timeoutState`
- BLE command '0' → `OpeningState` (manual reversal)
- BLE command '3' → `pausedState` (emergency stop)

---

## Configuration Parameters

| Parameter | Value | Description |
|-----------|-------|-------------|
| `LOOP_TIME_MS` | 100 ms | State processing period |
| `MAX_TRAVEL_MS` | 30000 ms | Absolute cap on one travel — see [Travel Cap](#2-travel-cap-max_travel_ms) |
| **Motor startup delay** | 500 ms | Wait time after motor start |

Settable from the app over characteristic `...0007`, stored in flash, clamped to the
bounds shown:

| Setting | Default | Range | Description |
|---------|---------|-------|-------------|
| `moveThreshold` | 6.0 deg/s | 1.0 – 50.0 | What counts as the door rotating |
| `stopTimeoutMs` | 1000 ms | 200 – 5000 | No rotation for this long ends the travel |
| `speed` | 255 | 5 – 255 | Motor PWM duty |
| `reversed` | 0 | 0 / 1 | Motor wired the other way round |

> These two govern when a travel is called finished, and nothing else stops the motor in
> normal operation. Set badly they can keep it running indefinitely, which is what
> [`MAX_TRAVEL_MS`](#2-travel-cap-max_travel_ms) exists to catch.

---

## End-of-Travel Detection

### Principle

Since the door rotates, the gyroscope detects angular movement. When the door reaches its end stop:
1. Rotation stops abruptly
2. Gyroscope drops below the `moveThreshold` setting
3. After `stopTimeoutMs` without movement → end-of-travel confirmed
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
| Name | `...0004` | Read, Write | up to 29 chars |
| State | `...0005` | Read, Notify | 1 byte, raw enum |
| Settings | `...0007` | Read, Write, Notify | 6 bytes, big-endian |

Note the asymmetry: commands are ASCII characters, everything else is raw bytes. The
commands are kept in ASCII because they read directly in the serial logs.

### Commands (`...0002`)

| Command | ASCII Code | Action |
|---------|------------|--------|
| Open | `'0'` (48) | Start opening |
| Close | `'1'` (49) | Start closing |
| Refresh | `'2'` (50) | Push the whole settings block **and** the current state |
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
| 7 | `DOOR_STATE_TIMEOUT` | travel cut short by the safety cap, position unknown |

### Settings (`...0007`)

Every persistent setting travels together, as one big-endian block:

| Bytes | Field | Meaning |
|-------|-------|---------|
| `[0]` | `speed` | motor PWM duty, 5–255 |
| `[1]` | `reversed` | 1 when the motor is wired the other way round |
| `[2:3]` | `stopTimeoutMs` | no rotation for this long ends the travel |
| `[4:5]` | `moveThreshold` | what counts as rotation, in tenths of a °/s |

One characteristic rather than one per setting: adding a parameter costs a field and two
lines of coding, instead of a characteristic, a write handler, a storage pair and a read
on the app side — in both board branches.

The block is written whole, so a partial write is refused rather than applied half way.
Each setter clamps to the bounds in `storage_hal.h`, and the board notifies the stored
block afterwards, so the app displays what actually took effect rather than what it sent.

`reversed` is applied in `Motor_Move()`, the single place both `OpeningState` and
`ClosingState` go through. `stopTimeoutMs` and `moveThreshold` govern the end-of-travel
detection described above — see the safety note there.

### Name (`...0004`)

The door name lives in flash (`pref_doorName`) and is advertised in the **scan
response** — not in the advertising packet, which the 128-bit service UUID nearly fills.
That caps it at `MAX_NAME_LEN` = 29 characters. Writing this characteristic renames the
board and restarts advertising; the board echoes back what it actually stored, which may
have been truncated. This is what lets the app tell several boards apart.

---

## Safety Features

### 1. Blockage Protection
If the door is blocked (obstacle, end stop), the gyroscope no longer detects movement →
stop after `stopTimeoutMs`

### 2. Travel Cap (`MAX_TRAVEL_MS`)

The backstop that holds when stop detection does not. Blockage protection above depends on
`moveThreshold` and `stopTimeoutMs`, and **both are settable from the app**, so both can be
set to values that never end a travel — a threshold below the gyroscope's noise floor makes
noise look like movement, the timer is rearmed on every iteration and the motor keeps
pushing the door against its stop. Nothing else would catch it: there is no motor current
measurement (see [Future Improvements](#future-improvements)).

`OpeningState` and `ClosingState` therefore each compare `millis() - travel_start` against
`MAX_TRAVEL_MS` **before** looking at the no-movement timer, and hand over to
`timeoutState` when it is exceeded. `travel_start` is set in the `enter()` of both states,
so the cap counts from the moment the motor started, not from the last movement seen.

It reports `DOOR_STATE_TIMEOUT` and not `OPEN` or `CLOSED`, because reaching it means the
travel did not complete normally and the door's position is unknown. The app shows
"Arrêt de sécurité".

`MAX_TRAVEL_MS` is a compile-time constant on purpose. It is not exposed over BLE: a
safety limit the app can raise is not a safety limit. Set it comfortably above the longest
normal travel — if it fires during ordinary use, the value is too low, not the door too
slow.

### 3. Emergency Stop
A BLE command can stop the motor at any time by changing state

### 4. No Unintended Movement
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

### Rotation Direction

Movement detection sums the absolute values of the three gyro axes
(`total_rot = |x| + |y| + |z|`), so it measures how much the door turns, never which
way — deliberately, to stay independent of how the board is mounted. The cost is that
a board whose reverse flag is set wrongly drives the door the wrong way, sees rotation
all the same, and reports `OPEN` once the travel completes while the door is closed.

Recovering the direction does **not** require modelling the hinge side, the opening
direction or the mounting orientation. The board is fixed to the door, so the rotation
axis is constant in the board's frame and those three variables compose into a single
constant vector that only has to be observed once:

- [ ] Record a normalised reference gyro vector during one opening travel, store it in
      flash (3 floats), then use `dot(reading, reference) > 0` to tell which way the
      door is turning. Mounting-, hinge- and swing-agnostic, one dot product per
      iteration.
- [ ] Bootstrap it from the `reversed` setting (`...0007`). The board cannot check on its
      own that the travel it is learning from really was an opening: all it knows is
      that it powered the motor in the direction it calls "open", and on a mis-wired
      motor that direction closes the door. It would then store a closing vector
      labelled "opening" — wrong, and self-consistent, so undetectable afterwards.
      Setting the reverse flag is the human vouching, once, that "open" really opens;
      only then is the observed travel known to be an opening. So this does **not**
      make the Normal/Inversé toggle redundant, it depends on it.
- [ ] Once the reference exists, use it as a guard: abort a travel that starts turning
      the wrong way instead of running it to completion and reporting the wrong state.

Note this adds a field to `flashPrefs`, hence another one-off reset of stored
preferences, and it can only be validated on a real door.

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