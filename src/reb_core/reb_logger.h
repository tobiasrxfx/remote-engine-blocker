/**
 * @file reb_logger.h
 * @brief Logging interface for the Remote Engine Blocker (REB) system.
 * @details Provides three severity levels for system event recording:
 *          info (normal operation), warn (unexpected but non-critical),
 *          and error (critical failures requiring attention).
 * @note Log output destination (UART, flash, RTT, etc.) is implementation-dependent.
 * @warning Logging functions should be non-blocking and safe to call from
 *          interrupts or timing-critical paths.
 */

#ifndef REB_LOGGER_H
#define REB_LOGGER_H

/*==============================================================================
 * Public Logging Functions
 *============================================================================*/

/**
 * @brief Logs an informational message.
 * @details Used for normal system events such as:
 *          - System startup and initialization
 *          - State transitions (e.g., "Engine blocked", "Engine released")
 *          - Configuration changes
 *          - Successful authentication
 * 
 * @param message [in] Null-terminated string containing the log message.
 *                     Must not be NULL.
 * 
 * @pre  Logger subsystem must be initialized (implementation-specific).
 * @post Message is recorded to configured output destination.
 * 
 * @note This function should not add any prefix or severity tag - that is
 *       the responsibility of the underlying logger implementation.
 * 
 * @note Examples: "System initialized", "Engine blocked by remote command",
 *                "Speed below threshold, block engaged"
 * 
 * @warning Do not pass string literals that exceed implementation's max
 *          message length (typically 128-256 bytes).
 */
void reb_logger_info(const char *message);

/**
 * @brief Logs a warning message.
 * @details Used for unexpected but recoverable conditions:
 *          - Invalid authentication attempts (below threshold)
 *          - Transient sensor reading anomalies
 *          - Communication timeouts (retrying)
 *          - Approaching critical limits (e.g., low battery, high RPM)
 * 
 * @param message [in] Null-terminated string containing the warning message.
 *                     Must not be NULL.
 * 
 * @pre  Logger subsystem must be initialized.
 * @post Warning is recorded; system continues normal operation.
 * 
 * @note Warnings typically indicate conditions that may lead to errors
 *       if left unaddressed, but do not require immediate shutdown.
 * 
 * @note Examples: "Authentication attempt #2 failed", "CAN bus timeout, retrying",
 *                "Battery voltage low (10.2V)", "RPM near limit (980)"
 * 
 * @warning Frequent warnings may indicate a developing fault - monitor
 *          warning rate in production systems.
 */
void reb_logger_warn(const char *message);

/**
 * @brief Logs an error message.
 * @details Used for critical failures that require attention:
 *          - Maximum invalid attempts exceeded (lockout triggered)
 *          - Sensor failure (invalid or stuck readings)
 *          - Nonce replay attack detected
 *          - Configuration corruption
 *          - Engine block engagement failure
 * 
 * @param message [in] Null-terminated string containing the error message.
 *                     Must not be NULL.
 * 
 * @pre  Logger subsystem must be initialized.
 * @post Error is recorded; system may enter failsafe mode depending on
 *       severity and context.
 * 
 * @note Errors typically trigger additional actions beyond logging:
 *       - Activating engine block
 *       - Raising external alert (LED, buzzer, telemetry)
 *       - Entering degraded operation mode
 * 
 * @note Examples: "MAX_INVALID_ATTEMPTS exceeded - lockout active",
 *                "Nonce replay attack detected", "Engine block relay stuck"
 * 
 * @warning Errors should be rare in normal operation. Frequent errors
 *          indicate a hardware fault or security breach.
 * 
 * @warning Do not call error logging inside an ISR if the implementation
 *          uses blocking I/O (e.g., flash write). Use a deferred logging
 *          mechanism instead.
 */
void reb_logger_error(const char *message);

#endif /* REB_LOGGER_H */