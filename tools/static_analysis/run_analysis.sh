@echo off
setlocal enabledelayedexpansion

set TOOLS_DIR=%CD%
set BUILD_DIR=%TOOLS_DIR%\build
set SRC_DIR=..\..\src\reb_core

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [STEP 1] Generating analysis artifacts...
cd "%SRC_DIR%"
cppcheck --dump *.c
move /y *.dump "%BUILD_DIR%" >nul 2>&1
move /y *.ctu-info "%BUILD_DIR%" >nul 2>&1

echo.
echo [STEP 2] Verifying MISRA C compliance...
cd "%BUILD_DIR%"

for %%f in (*.dump) do (
    echo Analyzing: %%f
    python "C:\Program Files\Cppcheck\addons\misra.py" --rule-text="%TOOLS_DIR%\rules.txt" "%%f"
)

cd "%TOOLS_DIR%"
echo.
echo --- Analysis Complete ---
pause