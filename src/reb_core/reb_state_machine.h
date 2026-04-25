/**
 * @file reb_state_machine.h
 * @brief State machine management for the Remote Engine Blocker (REB) system.
 * @details Defines and manages the system's operational states including:
 *          - NORMAL: Engine free, normal operation
 *          - BLOCKED: Engine forcibly disabled (theft prevention)
 *          - LOCKOUT: Security lockdown after excessive invalid attempts
 *          - DERATED: Reduced power mode (gradual engine limitation)
 *          - FAILSAFE: Emergency mode due to system fault
 * @note This module orchestrates the high-level behavior of the REB system.
 * @warning State transitions have safety and security implications.
 *          Always validate conditions before changing states.
 */

#ifndef REB_STATE_MACHINE_H
#define REB_STATE_MACHINE_H

/*==============================================================================
 * Dependencies
 *============================================================================*/

#include "reb_types.h"  /* Provides RebContext, RebInputs, RebOutputs */

/*==============================================================================
 * Public State Machine Functions
 *============================================================================*/

/**
 * @brief Initializes the REB state machine to a known safe state.
 * @details Sets the initial system state to NORMAL (engine free, no restrictions).
 *          Resets all state timers, transition counters, and flags.
 *          Must be called once before any call to reb_state_machine_step().
 * 
 * @param context [out] Pointer to the REB context containing state machine
 *                      variables (current_state, previous_state, state_timer,
 *                      state_entry_time). Must not be NULL.
 * 
 * @pre  The memory pointed by @p context must already be allocated.
 * @post context->current_state = REB_STATE_NORMAL
 * @post context->state_entry_time is set to current system time
 * @post All state-specific counters are reset to zero
 * 
 * @note This function is typically called from reb_core_init().
 * 
 * @note The initial state may be overridden by reb_persistence_load()
 *       if a lockout or blocked state was active before power loss.
 * 
 * @warning Calling this function while the system is running will
 *          forcibly reset the state machine, potentially releasing
 *          an active engine block. Only call during system startup.
 * 
 * @see reb_state_machine_step
 * @see reb_core_init
 */
void reb_state_machine_init(RebContext *context);

/**
 * @brief Executes one step of the REB state machine.
 * @details Evaluates current system inputs and context to determine if a
 *          state transition is required. Updates outputs based on the
 *          current state (block command, derating percentage, alerts).
 *          Handles state entry/exit actions and timeout management.
 * 
 * @param context [in,out] Pointer to REB context maintaining state machine
 *                         data (current_state, state_timer, invalid_attempts,
 *                         lockout_active, etc.). Must not be NULL.
 * @param inputs  [in]     Pointer to current system inputs (speed, RPM,
 *                         battery voltage, remote command flags, etc.).
 *                         Must contain valid, recent data. Must not be NULL.
 * @param outputs [out]    Pointer to output structure where actuator commands
 *                         will be written based on current state:
 *                         - block_engine: true for BLOCKED/LOCKOUT states
 *                         - derate_percent: 0-90% based on DERATED level
 *                         - alert_active: true for LOCKOUT/FAILSAFE
 *                         - led_pattern: visual indication of current state
 * 
 * @pre  reb_state_machine_init() must have been called.
 * @pre  context, inputs, and outputs must all be non-NULL valid pointers.
 * @post outputs are fully populated based on current state and inputs.
 * @post If a state transition occurred, context->state_entry_time is updated.
 * 
 * @note This function should be called at a fixed frequency (typically
 *       10-50 Hz) to ensure consistent timing for state timeouts.
 * 
 * @note The function is deterministic and uses no dynamic memory allocation.
 * 
 * @note State transition logic typically follows:
 *       - NORMAL → BLOCKED (valid remote block command)
 *       - NORMAL → DERATED (theft confirmation window active)
 *       - BLOCKED → NORMAL (valid remote unlock + safety check)
 *       - Any state → LOCKOUT (max invalid attempts exceeded)
 *       - Any state → FAILSAFE (critical sensor failure)
 * 
 * @warning Do not call this function from interrupt context if it runs longer
 *          than the sampling period. Use task/thread context instead.
 * 
 * @warning State transitions may have side effects such as logging,
 *          persistence writes, or external alerts. Ensure these are
 *          acceptable for the calling frequency.
 * 
 * @see reb_state_machine_init
 * @see reb_core_execute
 */
void reb_state_machine_step(RebContext *context,
                            const RebInputs *inputs,
                            RebOutputs *outputs);

#endif /* REB_STATE_MACHINE_H */