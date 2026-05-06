# REB — Remote Engine Blocker

Remote Engine Blocker (REB) project developed as an automotive embedded systems final project.

The system simulates an anti-theft remote vehicle blocking solution using CAN communication, embedded C logic, and a Python-based simulation environment.

---

## Overview

This repository contains:

- REB core logic implemented in C
- CAN communication layer and message adapters
- Virtual CAN bus simulation
- Interactive dashboard GUI for demonstration and validation
- Virtual ECUs (BCM, Engine ECU, TCU simulation)
- Automated demo scenarios
- Unit, integration, and functional tests
- Documentation, RTM, architecture, and test artifacts

---

## Main Features

### Security & Blocking Logic

Implemented REB state machine:

- `IDLE`
- `THEFT_CONFIRMED`
- `BLOCKING`
- `BLOCKED`

Supported operations:

- Remote Block
- Remote Unblock
- Manual Panel Block
- Panel Authentication Unlock
- Intrusion detection via BCM

Security mechanisms:

- Nonce validation
- Anti-replay protection
- Invalid unlock attempt counter

---

## Safe Vehicle Blocking Strategy

The REB respects safety requirements:

### Vehicle moving
When vehicle speed is above safe threshold:

- transition to `THEFT_CONFIRMED`
- visual/acoustic alerts (BCM scenario only)
- progressive torque/power derating
- vehicle slows down safely

Then:

- transition to `BLOCKING`
- when speed reaches zero:
  - starter lock enabled
  - prevent restart
  - transition to `BLOCKED`

### Vehicle stationary
For stationary remote/manual block:

- immediate transition to blocking sequence
- no unnecessary theft delay
- direct lock behavior according to FR001–FR003

---

## CAN Messages

Implemented CAN messages:

| ID | Message |
|---|---|
| `0x200` | REB_CMD |
| `0x201` | REB_STATUS |
| `0x300` | TCU_TO_REB / TIME_TICK |
| `0x400` | REB_DERATE_CMD |
| `0x401` | REB_PREVENT_START |
| `0x500` | VEHICLE_STATE |
| `0x501` | BCM_INTRUSION_STATUS |
| `0x503` | PANEL commands |

CAN definitions available in:

- `docs/can_messages.md`
- `docs/dbc/`

---

## Project Structure

```text
remote-engine-blocker/
│
├── src/                # Production C code
│   ├── app/
│   ├── reb_core/
│   ├── can/
│   └── common/
│
├── sim/                # Python simulation environment
│   ├── bus/
│   ├── dashboard/
│   ├── engine_ecu/
│   └── scenarios/
│
├── tests/              # Unit, integration and scenario tests
├── docs/               # Architecture, RTM, test plan, decisions
├── tools/              # Utility scripts
└── .github/            # CI/CD and templates
```

---

## Build Environment

Standardized on:

- GCC
- CMake
- Ninja
- Python 3.x

---

## Required Tools

- GCC
- CMake
- Ninja
- Python 3.x

---

## Windows Setup

Check WinGet:

```powershell
winget --version
```

Check toolchain:

```powershell
gcc --version
cmake --version
ninja --version
python --version
```

---

## Configure and Build

From repository root:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build
```

---

## Running the Simulation

### 1. Start virtual CAN bus

```powershell
py sim\bus\virtual_bus.py
```

---

### 2. Start REB server

Fresh simulation state:

```powershell
.\build\src\app\reb_app_loop.exe --fresh-start
```

---

### 3. Start REB bridge

```powershell
py sim\bus\reb_bridge.py
```

---

### 4. Start Engine ECU simulation

```powershell
py sim\engine_ecu\engine_ecu.py
```

---

### 5. Start dashboard GUI

```powershell
py sim\dashboard\test_dashboard.py
```

---

## Demo Scenarios

Supported demo flows:

### Remote theft while vehicle moving

Flow:

```text
IDLE
→ THEFT_CONFIRMED
→ BLOCKING
→ BLOCKED
```

Behavior:

- vehicle starts at speed
- remote block command issued
- progressive derating
- vehicle stops
- starter locked
- engine prevented from restart

---

### BCM intrusion scenario

Flow:

- intrusion detected
- alerts activated
- theft confirmation
- optional cancel/authentication flow

---

### Manual panel block

Flow:

- operator presses panel block
- immediate secure blocking behavior

---

### Unlock flows

Supported unlocks:

- Remote Unblock
- Panel Authentication

---

## Testing

Implemented:

- Unit tests
- Integration tests
- Functional scenario tests
- Automated demo scenarios

Validation includes:

- moving vs stationary vehicle
- invalid nonce handling
- replay prevention
- unlock authentication
- blocking transitions

---

## Static Analysis

Performed:

- MISRA-oriented static analysis
- violation categorization
- warning cleanup and review

---

## Requirements Audit (optional)

Run directly:

```bash
python3 tools/requirements_audit.py
```

Or via CMake:

```bash
cmake --build build --target requirements_audit
```

---

## Team Workflow

- branching from `dev`
- pull requests required
- conventional commits
- daily integration

---

## Status

✅ Final project completed

Implemented:

- embedded REB logic
- simulation environment
- GUI integration
- functional validation
- release artifacts

---

## License

Academic project for educational purposes.
