/**
 * @file reb_security.h
 * @brief Security and authentication module for the Remote Engine Blocker (REB) system.
 * @details Provides cryptographic validation of remote commands and authorization
 *          checks for engine unlock operations. Implements anti-replay protection,
 *          nonce validation, and brute-force attempt limiting.
 * @note All security-sensitive operations are centralized in this module.
 * @warning This module is the first line of defense against unauthorized engine
 *          control. Any security vulnerability here compromises the entire system.
 */

#ifndef REB_SECURITY_H
#define REB_SECURITY_H

/*==============================================================================
 * Dependencies
 *============================================================================*/

#include <stdbool.h>    /* Provides bool, true, false */
#include "reb_types.h"  /* Provides RebInputs, RebContext structures */

/*==============================================================================
 * Public Security Functions
 *============================================================================*/

/**
 * @brief Validates an incoming remote command for authenticity and integrity.
 * @details Performs comprehensive security checks on remote commands:
 *          - Cryptographic signature verification (authenticity)
 *          - Nonce replay attack detection (freshness)
 *          - Command integrity check (tamper detection)
 *          - Rate limiting / brute-force prevention
 * 
 * @param inputs  [in]     Pointer to current system inputs containing the
 *                         raw remote command data, nonce, and signature.
 *                         Must not be NULL.
 * @param context [in,out] Pointer to REB context maintaining security state:
 *                         - Nonce history for replay detection
 *                         - Invalid attempt counters
 *                         - Current lockout status
 * 
 * @return true  - Remote command is valid, authentic, and fresh.
 * @return false - Command invalid (bad signature, replay attack, or
 *                 maximum attempts exceeded).
 * 
 * @pre  reb_core_init() must have been called.
 * @pre  inputs->remote_command, inputs->nonce, and inputs->signature
 *       must contain valid data.
 * @post On success, the command is considered authorized for execution.
 * @post On failure, context->invalid_attempts is incremented. If threshold
 *       is reached, context->lockout_active is set to true.
 * 
 * @note This function implements the core anti-theft logic. Only commands
 *       that return true should trigger engine blocking or unlocking.
 * 
 * @note Nonce validation uses a sliding window of size REB_NONCE_WINDOW_SIZE
 *       to prevent replay attacks while allowing some out-of-order delivery.
 * 
 * @note Invalid attempt counter persists across power cycles (see
 *       reb_persistence_save) to prevent brute-force by power cycling.
 * 
 * @warning This function may be computationally expensive (cryptographic
 *          operations). Do not call from interrupt context.
 * 
 * @warning A false return value should ALWAYS be treated as invalid.
 *          Never override or ignore the result for security reasons.
 * 
 * @see reb_security_unlock_allowed
 * @see REB_MAX_INVALID_ATTEMPTS (in reb_config.h)
 * @see REB_NONCE_WINDOW_SIZE (in reb_config.h)
 */
bool reb_security_validate_remote_command(const RebInputs *inputs,
                                           RebContext *context);

/**
 * @brief Checks if engine unlock is allowed under current conditions.
 * @details Performs secondary authorization checks before allowing engine
 *          unlock, independent of the remote command validation:
 *          - System is not in lockout state (max attempts not exceeded)
 *          - Vehicle speed is within safe range for unlock
 *          - Engine RPM is not critically high
 *          - Battery voltage is sufficient for safe operation
 *          - Any additional geofencing or time-based restrictions
 * 
 * @param inputs  [in] Pointer to current system inputs containing vehicle
 *                     state (speed, RPM, battery voltage, GPS, etc.).
 *                     Must not be NULL.
 * @param context [in] Pointer to REB context containing system state
 *                     (lockout status, current block state, etc.).
 * 
 * @return true  - Engine unlock is permitted under current conditions.
 * @return false - Engine unlock denied (lockout active, unsafe speed,
 *                 low battery, or other safety condition).
 * 
 * @pre  inputs must contain valid, recent sensor readings.
 * @pre  context must be properly initialized.
 * @post Return value is purely informational - caller must enforce decision.
 * 
 * @note This function performs NO cryptographic validation. It assumes the
 *       command has already passed reb_security_validate_remote_command().
 * 
 * @note Safety takes precedence over convenience. Even with a valid remote
 *       command, unlock may be denied if conditions are unsafe.
 * 
 * @note Typical denial reasons (should be logged for diagnostics):
 *       - "Lockout active - too many invalid attempts"
 *       - "Speed too high for safe unlock (>5 km/h)"
 *       - "Battery voltage critical (<9.0V)"
 *       - "RPM above safe limit"
 * 
 * @warning This function must remain fast and deterministic. It is called
 *          on every unlock attempt and may be called frequently.
 * 
 * @warning Do NOT bypass this check even if the remote command is valid.
 *          Safety conditions (speed, RPM) can change rapidly and must be
 *          verified at the moment of unlock.
 * 
 * @see reb_security_validate_remote_command
 * @see REB_MAX_SPEED_FOR_BLOCK_KMH (in reb_config.h)
 * @see REB_MIN_BATTERY_VOLTAGE (in reb_config.h)
 * @see REB_ENGINE_RPM_LIMIT (in reb_config.h)
 */
bool reb_security_unlock_allowed(const RebInputs *inputs,
                                 RebContext *context);

#endif /* REB_SECURITY_H */