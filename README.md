# REB — Remote Engine Blocker

Remote Engine Blocker project developed as an automotive embedded systems final project.

## Overview

This repository contains:

- the REB core logic implemented in C
- a virtual CAN-based simulation environment
- a virtual vehicle dashboard for validation and demonstration
- documentation, tests, and traceability artifacts

## Project structure

- `src/` → REB production code in C
- `sim/` → simulation modules and GUI
- `tests/` → unit, integration, and scenario tests
- `docs/` → architecture, RTM, decisions, and test plan
- `.github/` → CI and contribution templates

## Build environment

This project is standardized on:

- GCC as the C compiler
- CMake as the build system generator
- Ninja as the build backend

### Required tools

- GCC
- CMake
- Ninja
- Python 3.x

### Windows setup

Check WinGet:
```powershell
winget --version
```
Check the compiler and build tools:
```powershell
gcc --version
cmake --version
ninja --version
```
If one of these is not properly installed, the build will not work. Otherwise, go to the next step.

### Configure and build
In the repository root `` remote-engine-blocker/`` run the following commands:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc
cmake --build build
```

## Run

Instructions will be added as the simulation environment is finalized.

## Team workflow

- branching from `dev`
- pull requests required
- conventional commits
- daily integration

## Status

Project setup phase.