# Static Analysis Infrastructure

This folder contains the tools and configurations for MISRA C:2012 compliance analysis of the Remote Engine Blocker (REB) core.

## Tools

- **run_analysis.bat**: Automated script for Windows environments.
- **run_analysis.sh**: Automated script for Linux and CI/CD pipelines.
- **reb_analysis.cppcheck**: Pre-configured GUI project for visual debugging.
- **rules.txt**: Descriptions for MISRA rules mapping.

## How to Use

### Terminal (Fast Check)
Run the script corresponding to your OS to see violations directly in the terminal:
- **Windows**: `.\run_analysis.bat`
- **Linux**: `./run_analysis.sh`

### GUI (Visual Debugging)
For a better experience while fixing violations:
1. Open **Cppcheck GUI**.
2. Go to `File > Open Project` and select `reb_analysis.cppcheck`.
3. Click on violations to jump directly to the source code line.

## Build Artifacts
All temporary files and analysis dumps are stored in the `build/` folder, which is ignored by Git to keep the repository clean.