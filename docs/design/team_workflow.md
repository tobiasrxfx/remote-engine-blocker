# Team Workflow

## Branches
- main: stable branch
- dev: integration branch
- feature/<name>
- fix/<name>
- docs/<name>
- test/<name>

## Commit convention
Use Conventional Commits:
- feat:
- fix:
- docs:
- test:
- refactor:
- build:
- style:
- chore:

## Pull requests
- PRs target `dev`
- at least one peer review
- build must pass before merge

## Build standard

All developers must use the same local build configuration.

### Official toolchain

- GCC
- CMake
- Ninja

### Official configure command

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc
```

### Official configure command
```powershell
cmake --build build
```
