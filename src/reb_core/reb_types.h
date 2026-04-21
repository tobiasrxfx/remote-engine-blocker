/**
 * @file reb_types.h
 * @brief Type definitions, enumerations, and core data structures for the
 *        Remote Engine Blocker (REB) system.
 * @details Defines all public types used across the REB modules, including
 *          state machine states, remote command types, input sensor data,
 *          output actuator commands, and persistent context structure.
 * @note This file is included by all REB modules. Keep dependencies minimal.
 * @warning Do not add function declarations here - this is for types only.
 */

#ifndef REB_TYPES_H
#define REB_TYPES_H

/*==============================================================================
 * Dependencies
 *============================================================================*/

#include <stdbool.h>    /* Provides bool, true, false */
#include <stdint.h>     /* Provides uint8_t, uint16_t, uint32_t */
#include "reb_config.h" /* Provides configuration constants */

/*==============================================================================
 * Enumerations - State Machine
 *============================================================================*/

/**
 * @brief Operational states of the Remote Engine Blocker system.
 * @details Defines all possible states in the REB state machine.
 */
typedef enum
{
    /**
     * @brief System is idle, engine is free to operate normally.
     * @details No active theft detection or engine blocking.
     *          Normal vehicle operation permitted.
     */
    REB_STATE_IDLE = 0,
    
    /**
     * @brief Theft attempt has been detected and is being confirmed.
     * @details System is within the confirmation window (REB_THEFT_CONFIRM_WINDOW_MS).
     *          Power derating may be active. Engine not yet fully blocked.
     * @see REB_THEFT_CONFIRM_WINDOW_MS
     */
    REB_STATE_THEFT_CONFIRMED,
    
    /**
     * @brief Engine blocking is in progress.
     * @details Block command has been issued, waiting for vehicle to reach
     *          safe speed (REB_MAX_SPEED_FOR_BLOCK_KMH) before full block.
     * @see REB_MAX_SPEED_FOR_BLOCK_KMH
     */
    REB_STATE_BLOCKING,
    
    /**
     * @brief Engine is fully blocked (disabled).
     * @details Starter lock engaged. Vehicle cannot move under its own power.
     *          Requires valid remote unlock command to exit.
     */
    REB_STATE_BLOCKED

} RebState;

/*==============================================================================
 * Enumerations - Remote Commands
 *============================================================================*/

/**
 * @brief Remote command types received from the TCU (Telematics Control Unit)
 *        or user remote control.
 * @details Commands are validated by reb_security_validate_remote_command()
 *          before being acted upon.
 */
typedef enum
{
    /**
     * @brief No remote command pending.
     */
    REB_REMOTE_NONE = 0,
    
    /**
     * @brief Command to block the engine (theft prevention).
     * @details Initiates engine blocking sequence.
     */
    REB_REMOTE_BLOCK,
    
    /**
     * @brief Command to unlock/release the engine block.
     * @details Requires passing security validation AND safety checks
     *          (reb_security_unlock_allowed).
     */
    REB_REMOTE_UNLOCK,
    
    /**
     * @brief Command to cancel a pending operation.
     * @details Aborts current theft confirmation or blocking sequence.
     *          Returns system to IDLE state.
     */
    REB_REMOTE_CANCEL

} RebRemoteCommand;

/*==============================================================================
 * Input Structure - Sensor and External Data
 *============================================================================*/

/**
 * @brief Input data structure containing all sensor readings and external inputs.
 * @details Passed to reb_core_execute() and state machine functions.
 *          All fields must be populated with current, valid data.
 * @note Timestamps should be monotonic (milliseconds since boot).
 */
typedef struct
{
    /* --- Security & Status Flags --- */
    
    /** @brief Physical intrusion detected (door/hood/ignition tampering). */
    bool intrusion_detected;
    
    /** @brief Vehicle ignition is currently ON. */
    bool ignition_on;
    
    /** @brief Engine is running (RPM > idle threshold). */
    bool engine_running;
    
    /** @brief Acknowledgment received from TCU for last status message. */
    bool tcu_ack_received;

    /* --- Analog Measurements --- */
    
    /** @brief Battery voltage in volts. Critical below REB_MIN_BATTERY_VOLTAGE. */
    float battery_voltage;
    
    /** @brief Current engine speed in RPM. Used for safety checks. */
    uint16_t engine_rpm;

    /* --- Speed & Motion --- */
    
    /** @brief Vehicle speed in kilometers per hour. */
    float vehicle_speed_kmh;

    /* --- Remote Control --- */
    
    /** @brief Incoming remote command (BLOCK, UNLOCK, CANCEL, or NONE). */
    RebRemoteCommand remote_command;

    /* --- Timing & Security --- */
    
    /** @brief Monotonic timestamp in milliseconds for the current input sample. */
    uint32_t timestamp_ms;
    
    /** @brief Cryptographic nonce for anti-replay protection. */
    uint32_t nonce;

} RebInputs;

/*==============================================================================
 * Output Structure - Actuator Commands
 *============================================================================*/

/**
 * @brief Output data structure containing actuator commands and alerts.
 * @details Populated by reb_core_execute() and consumed by hardware abstraction layer.
 */
typedef struct
{
    /* --- Alerts & Indicators --- */
    
    /** @brief Activate visual alert (e.g., flashing LED, dashboard warning). */
    bool visual_alert;
    
    /** @brief Activate acoustic alert (e.g., buzzer, siren). */
    bool acoustic_alert;

    /* --- Engine Controls --- */
    
    /** @brief Lock the starter motor (prevent engine cranking/starting). */
    bool starter_lock;

    /* --- Power Management --- */
    
    /**
     * @brief Engine power derating percentage (0-100%).
     * @details 0% = no derating (full power).
     *          100% = maximum derating (minimum power, engine barely running).
     *          Actual allowed range constrained by REB_DERATE_MIN_PERCENT
     *          and REB_DERATE_MAX_PERCENT from reb_config.h.
     * @see REB_DERATE_MIN_PERCENT
     * @see REB_DERATE_MAX_PERCENT
     */
    uint8_t derate_percent;

    /* --- Communication --- */
    
    /** @brief Request to send current status to TCU (Telematics Control Unit). */
    bool send_status_to_tcu;

} RebOutputs;

/*==============================================================================
 * Context Structure - Persistent System State
 *============================================================================*/

/**
 * @brief Core system context maintaining state across execution cycles.
 * @details Contains state machine data, timers, security history, and
 *          persistent information. Saved to non-volatile memory when critical
 *          state changes occur.
 * @note This structure is passed to all REB core functions.
 */
typedef struct
{
    /* --- State Machine --- */
    
    /** @brief Current operational state of the REB system. */
    RebState current_state;

    /* --- Timing & Confirmation --- */

    /** @brief True when theft confirmation came from an automatic intrusion trigger. */
    bool automatic_trigger_active;
    
    /** @brief Timestamp when theft was confirmed (state entry time). */
    uint32_t theft_confirmed_timestamp_ms;
    
    /** @brief Timestamp when vehicle stopped moving (for block engagement). */
    uint32_t vehicle_stopped_timestamp_ms;
    
    /** @brief Last transmission time for blocked status to TCU. */
    uint32_t last_blocked_retx_timestamp_ms;

    /* --- Anti-Replay Security --- */
    
    /** @brief Last successfully validated nonce. */
    uint32_t last_valid_nonce;
    
    /** @brief Circular buffer of recent nonces for replay detection. */
    uint32_t nonce_history[REB_NONCE_HISTORY_SIZE];
    
    /** @brief Current write index for nonce_history circular buffer. */
    uint8_t nonce_history_index;

    /* --- Brute-Force Protection --- */
    
    /** @brief Number of consecutive invalid unlock attempts. */
    uint8_t invalid_unlock_attempts;

    /* --- Persistence Flags --- */
    
    /** @brief Indicates if loaded persistent state is valid (not corrupted). */
    bool persisted_state_valid;

} RebContext;

#endif /* REB_TYPES_H */