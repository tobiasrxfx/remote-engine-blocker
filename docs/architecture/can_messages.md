# CAN Message Map

## 1. Purpose

This document defines the CAN communication interface for the Remote Engine Blocker (REB) system.

It specifies:

* message definitions
* signal layout
* timing and timeout rules
* validation policies

The goal is to provide a **stable communication contract** between all modules.

---

## 2. Nodes

The following nodes participate in the CAN network:

* `TCU` — Telematics Control Unit (remote commands)
* `REB` — Remote Engine Blocker (core logic)
* `ECU_FUEL` — Fuel control unit (derating)
* `ECU_MOTOR` — Engine control unit (start prevention)
* `BCM` — Body Control Module (intrusion sensors)
* `PANEL` — Dashboard / user interface
* `VEHICLE` — Vehicle state simulation (speed, ignition, etc.)

---

## 3. Naming Rules

* Messages: `UPPER_SNAKE_CASE`
* Signals: `lower_snake_case`
* Nodes: `UPPER_SNAKE_CASE`

---

## 4. CAN ID Allocation

| Range       | Usage                    |
| ----------- | ------------------------ |
| 0x100–0x10F | TCU ↔ REB commands & ACK |
| 0x110–0x11F | REB → TCU status         |
| 0x200–0x20F | BCM / PANEL signals      |
| 0x300–0x30F | Vehicle state            |
| 0x400–0x40F | REB → actuators          |

---

## 5. Message Definitions

---

### BO_ 0x100 REB_CMD: 8 TCU → REB

Remote authenticated command.

| Signal          | Bits  | Description         |
| --------------- | ----- | ------------------- |
| cmd_type        | 0–7   | Command type        |
| auth_ok         | 8–15  | Authentication flag |
| cmd_nonce       | 16–31 | Anti-replay counter |
| cmd_timestamp_s | 32–63 | Timestamp (seconds) |

#### cmd_type

* 0 = none
* 1 = confirm_theft
* 2 = block
* 3 = unblock

---

### BO_ 0x101 REB_ACK: 4 TCU → REB

Acknowledgment of command.

| Signal      | Bits  | Description    |
| ----------- | ----- | -------------- |
| ack_id      | 0–7   | Command ID     |
| ack_ok      | 8–15  | ACK result     |
| fail_reason | 16–23 | Failure reason |
| reserved    | 24–31 | Reserved       |

---

### BO_ 0x110 REB_STATUS: 8 REB → TCU

Periodic system status.

**Period: 100 ms**

| Signal          | Bits  | Description     |
| --------------- | ----- | --------------- |
| state_id        | 0–7   | REB state       |
| blocked_flag    | 8–15  | Blocking active |
| source_trigger  | 16–23 | Trigger source  |
| derating_active | 24–31 | Derating active |
| derate_pct      | 32–39 | Derating level  |
| parked_flag     | 40–47 | Vehicle parked  |
| fault_code      | 48–55 | Fault indicator |
| reserved        | 56–63 | Reserved        |

---

### BO_ 0x400 REB_DERATE_CMD: 2 REB → ECU_FUEL

Derating command.

**Period: 500 ms (while active)**

| Signal        | Bits | Description     |
| ------------- | ---- | --------------- |
| derate_enable | 0–7  | Enable derating |
| derate_pct    | 8–15 | Percentage      |

---

### BO_ 0x401 REB_PREVENT_START: 2 REB → ECU_MOTOR

Prevent engine start.

| Signal        | Bits | Description |
| ------------- | ---- | ----------- |
| prevent_start | 0–7  | Block start |
| blocked_flag  | 8–15 | REB active  |

---

### BO_ 0x300 VEHICLE_STATE: 8 VEHICLE → REB

Vehicle operational state.

| Signal                | Bits  | Description   |
| --------------------- | ----- | ------------- |
| vehicle_speed_kph_x10 | 0–15  | Speed ×10     |
| ignition_state        | 16–23 | Ignition mode |
| engine_running        | 24–31 | Engine status |
| engine_rpm            | 32–47 | RPM           |
| brake_pedal_pct       | 48–55 | Brake %       |
| reserved              | 56–63 | Reserved      |

---

### BO_ 0x200 BCM_INTRUSION_STATUS: 4 BCM → REB

Intrusion detection.

| Signal           | Bits  | Description      |
| ---------------- | ----- | ---------------- |
| glass_break_flag | 0–7   | Glass break      |
| door_intrusion   | 8–15  | Door intrusion   |
| accel_peak_flag  | 16–23 | Shock detection  |
| sensor_score_pct | 24–31 | Confidence score |

---

### BO_ 0x201 PANEL_AUTH_CMD: 3 PANEL → REB

Manual authentication.

| Signal           | Bits  | Description    |
| ---------------- | ----- | -------------- |
| manual_auth_ok   | 0–7   | Manual auth    |
| password_ok      | 8–15  | Password valid |
| panel_locked_out | 16–23 | Lockout        |

---

### BO_ 0x202 PANEL_CANCEL_CMD: 2 PANEL → REB

Cancel request.

| Signal         | Bits | Description     |
| -------------- | ---- | --------------- |
| cancel_request | 0–7  | Cancel          |
| owner_confirm  | 8–15 | Owner confirmed |

---

## 6. Enumerations

### state_id

* 0 = IDLE
* 1 = THEFT_CONFIRMED
* 2 = BLOCKING
* 3 = BLOCKED

### source_trigger

* 0 = NONE
* 1 = REMOTE
* 2 = PANEL
* 3 = AUTO_SENSOR

### ignition_state

* 0 = OFF
* 1 = ACC
* 2 = ON

### fail_reason

* 0 = NONE
* 1 = AUTH_FAILED
* 2 = NONCE_REPLAY
* 3 = STALE_TIMESTAMP
* 4 = INVALID_SIGNAL
* 5 = ACK_TIMEOUT

---

## 7. Timing and Timeout Rules

| Message           | Period | Timeout | Behavior       |
| ----------------- | ------ | ------- | -------------- |
| REB_CMD           | event  | 1000 ms | reject stale   |
| REB_ACK           | event  | 500 ms  | retry/fail     |
| REB_STATUS        | 100 ms | 300 ms  | mark stale     |
| REB_DERATE_CMD    | 500 ms | 1000 ms | disable safely |
| REB_PREVENT_START | event  | 1000 ms | hold safe      |
| VEHICLE_STATE     | 100 ms | 300 ms  | inhibit unsafe |
| BCM_INTRUSION     | event  | 1000 ms | ignore stale   |
| PANEL_AUTH        | event  | 1000 ms | ignore stale   |
| PANEL_CANCEL      | event  | 1000 ms | ignore stale   |

---

## 8. Validation Rules

* `REB_CMD` is accepted only if:

  * `auth_ok == 1`
  * nonce is not reused
  * timestamp is valid

* `VEHICLE_STATE`:

  * must be fresh to allow state transitions
  * invalid data blocks unsafe actions

* `REB_PREVENT_START`:

  * must never be issued while vehicle speed > 0

* `REB_DERATE_CMD`:

  * must respect minimum safe torque limit

* All frames:

  * must have correct DLC
  * must match expected ID
  * must pass signal validation

---

## 9. Notes

This specification represents the **initial version of the CAN interface**.

It must be reviewed and approved before:

* implementing pack/unpack logic
* integrating REB core
* developing simulation modules
