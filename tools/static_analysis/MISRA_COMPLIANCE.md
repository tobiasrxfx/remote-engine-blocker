# MISRA C:2012 Compliance Report

**Project:** Remote Engine Blocker (REB) Core  
**Analyzer:** Cppcheck 2.18.0 (MISRA Addon)  
**Status: COMPLIANT** (0 Violations / 5 Justified Deviations)

---

## 1. Executive Summary
This document provides the compliance evidence for the `reb_core` module according to the MISRA C:2012 guidelines. The analysis confirms that the core logic is free of high and medium-risk violations, ensuring code safety and reliability.

## 2. Analysis Scope
The following source files were included in this audit:
- `placeholder.c`
- `reb_can_adapter.c`
- `reb_core.c`
- `reb_logger.c`
- `reb_persistence.c`
- `reb_rules.c`
- `reb_security.c`
- `reb_state_machine.c`

## 3. Resolved Violations
Before finalizing the compliance process, several high and medium-impact violations were identified and refactored. The table below summarizes the key improvements made to the source code:

| Rule | Occurrences | Description |
| :--- | :---: | :--- |
| **7.1** | 2 | Octal constants shall not be used |
| **8.4** | 2 | A compatible declaration shall be visible when an object or function with external linkage is defined. |
| **15.5** | 16 | A function shall have a single point of exit at the end. |
| **15.6** | 4 | The body of an iteration-statement or a selection-statement shall be a compound-statement. |
| **15.7** | 2 | All if else if constructs shall be terminated with an else statement. |
| **17.7** | 11 | The value returned by a function having non-void return type shall be used. |
| **Subtotal** | **37** | **-** |


## 4. Technical Deviations and Rationale
The following rules were suppressed to allow the system to run within a Windows/POSIX simulation environment instead of a target microcontroller (ECU). These deviations are limited to non-embedded infrastructure (I/O, Time, and OS headers) and do not affect the safety of the core logic.

| Rule | Description | Technical Justification |
| :--- | :--- | :--- |
| **2.5** | Unused macro | Macros defined as part of the module's interface, intended for use by external integration layers not included in this standalone core analysis. |
| **8.7** | External linkage | Functions required to have external linkage as they constitute the module's Public API, being consumed by integration drivers and the application layer. |
| **17.3** | Implicit function declaration | A side effect of the Windows-specific implementation for the `mkdir` function. This is an environment-specific warning that does not impact the embedded logic safety. |
| **21.6** | Standard Library I/O | Use of standard I/O functions (`printf`, `fopen`, `fprintf`) is necessary at this stage to provide execution feedback and data persistence within the simulation environment. |
| **21.10** | Standard Library Time | The `<time.h>` library is utilized to provide high-resolution timestamps for the logging system, essential for debugging and event sequencing during simulation. |



## 5. Verification Evidence
The results can be reproduced by running the provided scripts:
- **Windows:** `tools/static_analysis/run_analysis.bat`
- **Linux:** `tools/static_analysis/run_analysis.sh`

**Final Output:**

> `[STEP 2] Verifying MISRA C compliance...`
> `Analyzing: placeholder.c.dump`
> `Checking placeholder.c.dump...`
> `Checking placeholder.c.dump, config ...`
> `Analyzing: reb_can_adapter.c.dump`
> `Checking reb_can_adapter.c.dump...`
> `Checking reb_can_adapter.c.dump, config ...`
> `Analyzing: reb_core.c.dump`
> `Checking reb_core.c.dump...`
> `Checking reb_core.c.dump, config ...`
> `Analyzing: reb_logger.c.dump`
> `Checking reb_logger.c.dump...`
> `Checking reb_logger.c.dump, config ...`
> `Checking reb_logger.c.dump, config _WIN32;__CYGWIN__...`
> `Analyzing: reb_persistence.c.dump`
> `Checking reb_persistence.c.dump...`
> `Checking reb_persistence.c.dump, config ...`
> `Analyzing: reb_rules.c.dump`
> `Checking reb_rules.c.dump...`
> `Checking reb_rules.c.dump, config ...`
> `Analyzing: reb_security.c.dump`
> `Checking reb_security.c.dump...`
> `Checking reb_security.c.dump, config ...`
> `Analyzing: reb_state_machine.c.dump`
> `Checking reb_state_machine.c.dump...`
> `Checking reb_state_machine.c.dump, config ...`
> ``
> `--- Analysis Complete ---`
> `Logs are shown above and artifacts are located in: `
> `tools\static_analysis\build`
> `Pressione qualquer tecla para continuar. . . `
