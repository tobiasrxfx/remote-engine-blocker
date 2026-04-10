/**
 * @file reb_persistence.h
 * @brief Non-volatile storage interface for the Remote Engine Blocker (REB) system.
 * @details Provides functions to save and load system state across power cycles.
 *          Preserves critical data such as invalid attempt counters, lockout status,
 *          authentication state, and configuration overrides.
 * @note Persistence backend (EEPROM, Flash, FRAM, etc.) is implementation-dependent.
 * @warning Data integrity is critical for security - consider checksums or CRC
 *          validation in the implementation.
 */

#ifndef REB_PERSISTENCE_H
#define REB_PERSISTENCE_H

/*==============================================================================
 * Dependencies
 *============================================================================*/

#include "reb_types.h"  /* Provides RebContext structure definition */
#include <stdbool.h>    /* Provides bool, true, false */

/*==============================================================================
 * Public Persistence Functions
 *============================================================================*/

/**
 * @brief Saves the current REB context to non-volatile memory.
 * @details Persists security-critical state including:
 *          - Number of consecutive invalid authentication attempts
 *          - Current lockout status (active/inactive)
 *          - Blocked engine state
 *          - Any configuration overrides or learned parameters
 * 
 * @param context [in] Pointer to the REB context containing state to be saved.
 *                     Must not be NULL.
 * 
 * @return true  - Save operation completed successfully.
 * @return false - Save operation failed (storage error, CRC mismatch, etc.).
 * 
 * @pre  reb_core_init() must have been called at least once.
 * @pre  Persistent storage must be initialized and writable.
 * @post On success, context data is safely stored and will survive power loss.
 * @post On failure, context remains unchanged and error should be logged.
 * 
 * @note This function should be called after any state change that must
 *       survive a power cycle, such as:
 *       - After incrementing invalid attempt counter
 *       - When lockout becomes active
 *       - When engine block state changes
 * 
 * @note To avoid excessive flash wear, consider rate-limiting save operations
 *       or using a dirty flag with periodic save (e.g., every 5 seconds).
 * 
 * @note Typical flash memory supports 10,000 - 100,000 write cycles.
 *       Do not call this function on every control loop iteration.
 * 
 * @warning This function may be blocking and should not be called from
 *          interrupt context or time-critical paths.
 * 
 * @warning If save fails, the system should continue operating but log
 *          the error. State will be lost on power cycle.
 * 
 * @see reb_persistence_load
 * @see RebContext
 */
bool reb_persistence_save(const RebContext *context);

/**
 * @brief Loads previously saved REB context from non-volatile memory.
 * @details Restores security-critical state after power-up or system reset:
 *          - Invalid attempt counter (to maintain lockout across power cycles)
 *          - Active lockout status (prevents power-cycle bypass attacks)
 *          - Last known engine block state
 * 
 * @param context [out] Pointer to REB context where loaded data will be stored.
 *                      Must not be NULL.
 * 
 * @return true  - Load operation successful, context contains saved state.
 * @return false - Load operation failed (no valid data, corruption, CRC error).
 * 
 * @pre  Persistent storage must be initialized and readable.
 * @post On success, context is populated with previously saved values.
 * @post On failure, context should be initialized to safe defaults by the caller.
 * 
 * @note Should be called once during system initialization, immediately after
 *       reb_core_init() or as part of it.
 * 
 * @note If no valid saved state exists (first boot), this function should
 *       return false and the caller should use default initialization.
 * 
 * @note Implementation should include integrity verification (checksum/CRC)
 *       to detect data corruption.
 * 
 * @note To prevent rollback attacks, consider storing a monotonic counter
 *       or version number alongside the data.
 * 
 * @warning Do not call this function while the system is actively controlling
 *          the engine - loading may overwrite runtime state unexpectedly.
 * 
 * @warning A corrupted save returning false should trigger security measures,
 *          such as entering a degraded mode or requiring re-authentication.
 * 
 * @see reb_persistence_save
 * @see reb_core_init
 * @see RebContext
 */
bool reb_persistence_load(RebContext *context);

#endif /* REB_PERSISTENCE_H */