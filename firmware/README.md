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
│   Stopped   │◄─────────────┐
└──────┬──────┘              │
       │                     │
       │ BLE: Open ('0')     │ Timeout (1s no movement)
       ▼                     │
┌─────────────┐              │
│   Opening   ├──────────────┤
└──────┬──────┘              │
       │                     │
       │ BLE: Close ('1')    │
       ▼                     │
┌─────────────┐              │
│   Closing   ├──────────────┘
└─────────────┘
```

## States

### 1. StartupState

**Role**: System initialization at boot

**Actions**:
- Initialize IMU with automatic gyroscope calibration
- Load preferences from flash (motor speed)
- Initialize BLE (name: "CESAM")
- Stop motor for safety

**Transition**: → `StoppedState` automatically after init

---

### 2. StoppedState

**Role**: Idle state, motor stopped, waiting for BLE commands

**Actions**:
- Motor stopped
- Listening for BLE commands

**Transitions**:
- BLE command '0' → `OpeningState`
- BLE command '1' → `ClosingState`
- BLE command '2' → Refresh (stays in StoppedState)

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
- 1s timeout without movement → `StoppedState`
- BLE command '1' → `ClosingState` (manual reversal)
- BLE command → `StoppedState` (emergency stop)

---

### 4. ClosingState

**Role**: Door closing in progress

**Actions**: Identical to `OpeningState` but reversed rotation direction

**Transitions**:
- 1s timeout without movement → `StoppedState`
- BLE command '0' → `OpeningState` (manual reversal)
- BLE command → `StoppedState` (emergency stop)

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

| Command | ASCII Code | Action |
|---------|------------|--------|
| Open | `'0'` (48) | Start opening |
| Close | `'1'` (49) | Start closing |
| Refresh | `'2'` (50) | Return current motor speed |

**Service UUID**: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`  
**Characteristic UUID (commands)**: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`  
**Characteristic UUID (speed)**: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

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