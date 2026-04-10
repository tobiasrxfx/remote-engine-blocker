/**
 * @file reb_config.h
 * @brief Configuration parameters for the Reverse Electric Brake (REB) system.
 * @note All timing values are in milliseconds, speeds in km/h or m/s as specified.
 * @warning Do not modify these values without reviewing system safety constraints.
 */

#ifndef REB_CONFIG_H
#define REB_CONFIG_H

/*==============================================================================
 * 1. Timing & Confirmation Windows
 *============================================================================*/

/**
 * @def REB_THEFT_CONFIRM_WINDOW_MS
 * @brief Time window to confirm a theft attempt before triggering protection.
 * @details After detecting a potential theft condition, the system waits this
 *          duration to validate the attempt, avoiding false positives.
 * @unit milliseconds
 * @range 60000 (60 seconds)
 */
#define REB_THEFT_CONFIRM_WINDOW_MS      (60000U)

/**
 * @def REB_STOP_HOLD_TIME_MS
 * @brief Minimum duration the stop command must be held active.
 * @details Ensures the vehicle remains stopped for this period before allowing
 *          normal operation to resume, improving safety during restart.
 * @unit milliseconds
 * @range 120000 (2 minutes)
 */
#define REB_STOP_HOLD_TIME_MS            (120000U)

/**
 * @def REB_BLOCKED_RETRANSMIT_MS
 * @brief Retransmission interval when the system is blocked.
 * @details How often the system re-sends blocked status notifications to the
 *          controller while in a blocked state.
 * @unit milliseconds
 * @range 5000 (5 seconds)
 */
#define REB_BLOCKED_RETRANSMIT_MS        (5000U)

/*==============================================================================
 * 2. Speed Thresholds
 *============================================================================*/

/**
 * @def REB_MAX_ALLOWED_SPEED_FOR_LOCK
 * @brief Maximum vehicle speed to allow a mechanical lock engagement.
 * @details Prevents lock activation above this speed to avoid mechanical damage
 *          or loss of control.
 * @unit meters per second (m/s)
 * @range 0.5 m/s (~1.8 km/h)
 * @warning Exceeding this speed before lock can cause system failure.
 */
#define REB_MAX_ALLOWED_SPEED_FOR_LOCK   (0.5f)

/**
 * @def REB_SAFE_MOVING_SPEED_KMH
 * @brief Speed threshold considered as "safe moving" for derating decisions.
 * @details Used to determine when the vehicle is moving safely enough to
 *          allow reduced power modes.
 * @unit kilometers per hour (km/h)
 * @range 5.0 km/h
 */
#define REB_SAFE_MOVING_SPEED_KMH        (5.0f)

/**
 * @def REB_MAX_SPEED_FOR_BLOCK_KMH
 * @brief Maximum allowed speed to enter the blocked (locked) state.
 * @details If speed exceeds this value, the system will not transition to
 *          blocked state.
 * @unit kilometers per hour (km/h)
 * @range 5.0 km/h
 */
#define REB_MAX_SPEED_FOR_BLOCK_KMH      (5.0f)

/*==============================================================================
 * 3. Power Derating Parameters
 *============================================================================*/

/**
 * @def REB_DERATE_STEP_PERCENT
 * @brief Step percentage for gradual power derating.
 * @details Each derating step reduces power by this percentage.
 * @unit percent (%)
 * @range 10 (10% per step)
 */
#define REB_DERATE_STEP_PERCENT          (10U)

/**
 * @def REB_DERATE_MAX_PERCENT
 * @brief Maximum allowed power derating.
 * @details The system will not derate power beyond this percentage.
 * @unit percent (%)
 * @range 90 (90% derating → 10% power remaining)
 */
#define REB_DERATE_MAX_PERCENT           (90U)

/**
 * @def REB_DERATE_MIN_PERCENT
 * @brief Minimum required power derating (lower bound for safety).
 * @details Prevents the system from operating below this power level.
 * @unit percent (%)
 * @range 20 (at least 20% derating applied)
 */
#define REB_DERATE_MIN_PERCENT           (20U)

/*==============================================================================
 * 4. Security & Authentication
 *============================================================================*/

/**
 * @def REB_MAX_INVALID_ATTEMPTS
 * @brief Maximum number of consecutive invalid authentication attempts.
 * @details Exceeding this limit triggers a security lockout.
 * @range 3 attempts
 */
#define REB_MAX_INVALID_ATTEMPTS         (3U)

/**
 * @def REB_NONCE_WINDOW_SIZE
 * @brief Size of the sliding window for nonce validation.
 * @details Number of recent nonces kept in memory to prevent replay attacks.
 * @range 32 entries
 */
#define REB_NONCE_WINDOW_SIZE            (32U)

/**
 * @def REB_NONCE_HISTORY_SIZE
 * @brief Total historical nonces stored for anti-replay protection.
 * @details Older nonces beyond this size are discarded.
 * @range 10 entries
 */
#define REB_NONCE_HISTORY_SIZE           (10U)

/**
 * @def REB_NONCE_MAX_VALUE
 * @brief Maximum allowed value for a nonce.
 * @details Nonces must be <= this value. Used for overflow detection.
 * @range 0xFFFFFFFF (32-bit max)
 */
#define REB_NONCE_MAX_VALUE              (0xFFFFFFFFU)

/*==============================================================================
 * 5. Electrical & Mechanical Limits
 *============================================================================*/

/**
 * @def REB_MIN_BATTERY_VOLTAGE
 * @brief Minimum battery voltage required for REB operation.
 * @details Below this voltage, the system enters failsafe mode.
 * @unit volts (V)
 * @range 9.0 V
 */
#define REB_MIN_BATTERY_VOLTAGE          (9.0f)

/**
 * @def REB_ENGINE_RPM_LIMIT
 * @brief Maximum allowed engine RPM while REB is active.
 * @details If RPM exceeds this limit, the system forces a controlled stop.
 * @unit RPM (revolutions per minute)
 * @range 1000 RPM
 */
#define REB_ENGINE_RPM_LIMIT             (1000U)

#endif /* REB_CONFIG_H */