/**
 * @file reb_core.h
 * @brief Core state machine and execution logic for the Remote Engine Blocker (REB) system.
 * @details This module implements the main control algorithm for remote engine blocking,
 *          including theft prevention, speed-based blocking rules, power derating,
 *          and security lockout mechanisms.
 * @note Requires reb_types.h for context, input, and output structure definitions.
 * @warning This is a critical security module - review all changes with system architect.
 */

#ifndef REB_CORE_H
#define REB_CORE_H

/*==============================================================================
 * Dependencies
 *============================================================================*/

#include "reb_types.h"  /* Provides RebContext, RebInputs, RebOutputs */

/*==============================================================================
 * Public Functions
 *============================================================================*/

/**
 * @brief Initializes the REB core subsystem.
 * @details Resets the internal state machine, clears all flags, timers, and
 *          security counters. Engine blocking is disabled after initialization.
 *          Must be called once before any call to reb_core_execute().
 * 
 * @param context [out] Pointer to the REB context structure to be initialized.
 *                      Must not be NULL.
 * 
 * @pre  The memory pointed by @p context must already be allocated.
 * @post All internal counters and state variables are set to their default
 *       (safe, non-blocking) initial values.
 * 
 * @note This function is not thread-safe. Ensure exclusive access if using
 *       in a multi-threaded environment.
 * 
 * @warning Calling this function while the system is active may cause
 *          abrupt loss of blocking state. Only call during system startup or
 *          controlled reset.
 * 
 * @see reb_core_execute
 */
void reb_core_init(RebContext *context);

/**
 * @brief Executes one iteration of the REB control algorithm.
 * @details Reads current vehicle inputs (speed, RPM, battery voltage, etc.),
 *          applies theft detection logic, determines required engine blocking
 *          or power derating level, and generates appropriate output commands.
 * 
 * @param context [in,out] Pointer to the REB context maintaining state between
 *                         calls. Holds timers, attempt counters, nonce history.
 * @param inputs  [in]     Pointer to current sensor readings and user inputs.
 *                         Must contain valid, recent data.
 * @param outputs [out]    Pointer to structure where resulting actuator commands
 *                         will be written (block command, deration percent,
 *                         alert flags, ignition cut).
 * 
 * @pre  reb_core_init() must have been called successfully before first execute.
 * @pre  context, inputs, and outputs must all be non-NULL valid pointers.
 * @post outputs structure is fully populated based on current state and inputs.
 * 
 * @note This function should be called at a fixed frequency (typically 10-100 Hz)
 *       to ensure consistent timing for theft confirmation window and hold times.
 * 
 * @note The function is deterministic and does not allocate dynamic memory.
 * 
 * @note Engine blocking is only activated when speed is below
 *       REB_MAX_SPEED_FOR_BLOCK_KMH (configurable in reb_config.h).
 * 
 * @warning Do not call this function from interrupt context if it runs longer
 *          than the sampling period. Use task/thread context instead.
 * 
 * @warning Invalid inputs (e.g., out-of-range speed values) will trigger
 *          failsafe mode, applying engine block and raising alert.
 * 
 * @see reb_core_init
 * @see RebInputs
 * @see RebOutputs
 * @see RebContext
 */
void reb_core_execute(RebContext *context,
                      const RebInputs *inputs,
                      RebOutputs *outputs);

#endif /* REB_CORE_H */