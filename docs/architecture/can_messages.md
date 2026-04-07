# CAN Message Map — REB Project

## 1. Purpose

This document defines the CAN communication interface for the Remote Engine Blocker (REB) system.

It complements the project `.dbc` file by providing:

* architectural context
* timing and timeout rules
* signal validation policies
* simulation-specific extensions

**Important:**
The `.dbc` file is the **source of truth** for:

* CAN IDs
* signal layout
* bit positions
* scaling
* enumerations

This document must remain consistent with the `.dbc`.

---

## 2. Nodes

The following nodes participate in the CAN network:


* `REB` — Remote Engine Blocker (core logic)

### Simulation Nodes 


* `TCU` — Telematics Control Unit (remote commands)
* `ECU_FUEL` — Fuel control unit (derating)
* `ECU_MOTOR` — Engine control unit (start prevention)
* `BCM` — Intrusion detection (glass break, door, acceleration)
* `PANEL` — User interface (manual authentication / cancel)
* `VEHICLE` — Vehicle state (speed, ignition, RPM)

---

## 3. Naming Rules

* Messages: `UPPER_SNAKE_CASE`
* Signals: `lower_snake_case`
* Nodes: `UPPER_SNAKE_CASE`

---

## 4. CAN ID Allocation (Aligned with DBC)

| Message           | ID (hex) | ID (dec) |
| ----------------- | -------- | -------- |
| REB_CMD           | 0x200    | 512      |
| REB_STATUS        | 0x201    | 513      |
| TCU_TO_REB        | 0x300    | 768      |
| REB_DERATE_CMD    | 0x400    | 1024     |
| REB_PREVENT_START | 0x401    | 1025     |
| REB_GPS_REQUEST   | 0x402    | 1026     |

---

## 5. Message Definitions (DBC-Aligned)

---

### BO_ 0x200 REB_CMD: 8 TCU → REB

Authenticated command from TCU.

| Signal        | Description         |
| ------------- | ------------------- |
| cmd_type      | Command type        |
| cmd_mode      | Mode of operation   |
| cmd_nonce     | Anti-replay counter |
| cmd_timestamp | Timestamp           |

---

### BO_ 0x201 REB_STATUS: 8 REB → TCU

Periodic REB system status.

**Period: 100 ms**

| Signal        | Description     |
| ------------- | --------------- |
| status_code   | REB state       |
| blocked_flag  | Blocking active |
| vehicle_speed | Vehicle speed   |
| error_code    | Error indicator |
| reserved      | Reserved        |

---

### BO_ 0x300 TCU_TO_REB: 8 TCU → REB

ACK/NACK response from TCU.

| Signal         | Description               |
| -------------- | ------------------------- |
| tcu_cmd        | ACK/NACK/RETRY            |
| fail_reason    | Failure reason            |
| echo_timestamp | Echo of command timestamp |

---

### BO_ 0x400 REB_DERATE_CMD: 8 REB → ECU_FUEL

Fuel derating command.

**Period: 500 ms (while active)**

| Signal      | Description         |
| ----------- | ------------------- |
| derate_pct  | Derating percentage |
| derate_mode | Derating strategy   |
| safety_flag | Safety indicator    |

---

### BO_ 0x401 REB_PREVENT_START: 8 REB → ECU_MOTOR

Prevents engine start.

| Signal         | Description                |
| -------------- | -------------------------- |
| prevent_start  | Start allowed/blocked      |
| auth_token_lsb | Authentication token (LSB) |

---

### BO_ 0x402 REB_GPS_REQUEST: 8 REB → TCU

Request GPS data from TCU.

| Signal        | Description       |
| ------------- | ----------------- |
| gps_request   | Request flag      |
| state_id_echo | Current REB state |

---

## 6. Enumerations (Aligned with DBC)

### cmd_type

* 0 = NOP
* 1 = BLOCK
* 2 = UNBLOCK
* 3 = STATUS_REQUEST

### cmd_mode

* 0 = IDLE_MODE
* 1 = GRADUAL_DERATE
* 2 = FULL_BLOCK
* 3 = EMERGENCY

### status_code

* 0 = IDLE
* 1 = THEFT_CONFIRMED
* 2 = BLOCKING
* 3 = BLOCKED

### tcu_cmd

* 0 = ACK
* 1 = NACK
* 2 = RETRY
* 3 = FAIL

### derate_mode

* 0 = OFF
* 1 = GRADUAL_RAMP
* 2 = STEP
* 3 = IMMEDIATE

### prevent_start

* 0 = ALLOW
* 1 = BLOCK

### gps_request

* 0 = NO_REQUEST
* 1 = REQUEST_GPS

---

## 7. Timing and Timeout Rules

| Message           | Period | Timeout | Behavior       |
| ----------------- | ------ | ------- | -------------- |
| REB_CMD           | event  | 1000 ms | reject stale   |
| TCU_TO_REB        | event  | 500 ms  | retry/fail     |
| REB_STATUS        | 100 ms | 300 ms  | mark stale     |
| REB_DERATE_CMD    | 500 ms | 1000 ms | disable safely |
| REB_PREVENT_START | event  | 1000 ms | hold safe      |
| REB_GPS_REQUEST   | event  | 1000 ms | retry optional |

---

## 8. Validation Rules

### REB_CMD

* must have valid nonce (no replay)
* timestamp must be fresh
* command must be recognized

### TCU_TO_REB (ACK)

* must match a previous command
* timeout triggers retry or failure

### REB_STATUS

* must be transmitted periodically
* stale status must be detected by TCU

### REB_DERATE_CMD

* must respect safety limits
* must not exceed defined derate range

### REB_PREVENT_START

* must never be issued while vehicle is moving

### General Rules

* DLC must match expected size
* unknown IDs must be ignored
* invalid frames must be rejected

---

## 9. Simulation Extensions (Not Yet in DBC)

The following messages are required for the virtual vehicle simulation but are not yet defined in the `.dbc`:

* `VEHICLE_STATE`
* `BCM_INTRUSION_STATUS`
* `PANEL_AUTH_CMD`
* `PANEL_CANCEL_CMD`

These should be added in a future DBC revision.

---

## 10. Notes

This document represents the **initial CAN architecture specification**.

Before implementation:

* message definitions must be validated by the team
* inconsistencies between `.dbc` and this document must be resolved

The `.dbc` remains the authoritative source for frame encoding.
