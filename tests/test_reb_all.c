/**
 * @file test_reb_all.c
 * @brief Unity test suite for the Remote Engine Blocker (REB) system.
 *
 * @details This file contains the complete unit test suite for the REB firmware
 *          modules. Tests are organised into eighteen numbered groups, each
 *          targeting a specific module or behavioural aspect of the system.
 *
 *          Modules under test:
 *          - src/reb_core/reb_state_machine.c
 *          - src/reb_core/reb_security.c
 *          - src/reb_core/reb_core.c
 *          - src/reb_core/reb_rules.c
 *          - src/reb_core/reb_config.h   (constant validation)
 *          - src/reb_core/reb_types.h    (type and struct validation)
 *          - src/reb_core/reb_logger.c   (interface robustness)
 *          - src/reb_core/reb_persistence.c
 *
 *          Coverage targets:
 *          - Statement  >= 99 %
 *          - Branch     >= 90 %
 *          - MC/DC      >= 85 %
 *
 *          Build command (without CAN adapter):
 *          @code
 *          gcc -O0 --coverage \
 *              -I src/reb_core -I Unity/src \
 *              src/reb_core/reb_state_machine.c \
 *              src/reb_core/reb_security.c \
 *              src/reb_core/reb_core.c \
 *              src/reb_core/reb_rules.c \
 *              reb_logger.c  reb_persistence.c \
 *              test_reb_all.c \
 *              -o test_reb_all -lm
 *          @endcode
 *
 *          Coverage report:
 *          @code
 *          gcov -b -o . src/reb_core/reb_state_machine.c
 *          gcov -b -o . src/reb_core/reb_security.c
 *          gcov -b -o . src/reb_core/reb_core.c
 *          gcov -b -o . src/reb_core/reb_rules.c
 *          @endcode
 *
 * @note Persistence tests use real file I/O to the path
 *       @c artifacts/reb_state.bin. The helper @c _persist_prepare_dir()
 *       creates the @c artifacts/ directory when needed. Tests that write
 *       the persistence file call @c _persist_clear() in teardown to avoid
 *       cross-test interference.
 *
 * @see REB-SRS-001 v0.2 — System/Software Requirements Specification
 */

#include "unity.h"
#include "reb_types.h"
#include "reb_config.h"
#include "reb_state_machine.h"
#include "reb_security.h"
#include "reb_core.h"
#include "reb_rules.h"
#include "reb_persistence.h"
#include "reb_logger.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <float.h>
#include <sys/stat.h>   /* mkdir, chmod POSIX */

/*==============================================================================
 * Persistence helpers
 *==============================================================================*/

/**
 * @def BIN_PATH
 * @brief File path used by @c reb_persistence_save() and @c reb_persistence_load()
 *        for storing the serialised @c RebContext across calls.
 */
#define BIN_PATH "artifacts/reb_state.bin"

/**
 * @brief Creates the @c artifacts/ directory required by the persistence module.
 * @details Called before any test that writes the persistence file. Uses the
 *          POSIX @c mkdir() call; a failure (directory already exists) is silently
 *          ignored.
 */
static void _persist_prepare_dir(void)
{
    mkdir("artifacts", 0755);
}

/**
 * @brief Saves a @c RebContext to the persistence file, creating the directory
 *        if necessary.
 * @details Wraps @c reb_persistence_save() with directory creation so that tests
 *          can pre-populate a known state without caring about directory existence.
 * @param ctx [in] Pointer to the context to be serialised.
 */
static void _persist_write(const RebContext *ctx)
{
    _persist_prepare_dir();
    reb_persistence_save(ctx);
}

/**
 * @brief Removes the persistence file, simulating a first-boot condition.
 * @details After this call @c reb_persistence_load() will return @c false,
 *          causing @c reb_core_init() to fall back to @c reb_state_machine_init().
 */
static void _persist_clear(void)
{
    remove(BIN_PATH);
}

/**
 * @brief Loads the context from the persistence file.
 * @param out [out] Pointer to the @c RebContext that will receive the loaded data.
 * @return @c true if the file exists and the CRC is valid, @c false otherwise.
 */
static bool _persist_read(RebContext *out)
{
    return reb_persistence_load(out);
}

/*==============================================================================
 * Test helpers and shared constants
 *==============================================================================*/

/**
 * @def T_INTRUSION
 * @brief Arbitrary base timestamp (ms) used as the moment a theft event is
 *        injected into the state machine during test setup.
 */
#define T_INTRUSION   (1000U)

/**
 * @def T_CONFIRMED
 * @brief Timestamp at which the theft-confirmation window expires, computed as
 *        @c T_INTRUSION + @c REB_THEFT_CONFIRM_WINDOW_MS. Driving the state
 *        machine with this timestamp advances it from @c REB_STATE_THEFT_CONFIRMED
 *        to @c REB_STATE_BLOCKING.
 */
#define T_CONFIRMED   (T_INTRUSION + REB_THEFT_CONFIRM_WINDOW_MS)

/**
 * @def T_BLOCKED_TS
 * @brief Timestamp at which the safe-stop dwell completes, computed as
 *        @c T_CONFIRMED + 100 ms (first stop step) + @c REB_STOP_HOLD_TIME_MS.
 *        Driving the state machine with this timestamp advances it from
 *        @c REB_STATE_BLOCKING to @c REB_STATE_BLOCKED.
 */
#define T_BLOCKED_TS  (T_CONFIRMED + 100U + REB_STOP_HOLD_TIME_MS)

/**
 * @brief Initialises a @c RebContext to the IDLE state with all fields zeroed.
 * @details Used by test functions that need a clean context without triggering
 *          the full persistence path exercised by @c reb_core_init().
 * @param ctx [out] Pointer to the context to be initialised.
 */
static void ctx_init(RebContext *ctx)
{
    memset(ctx, 0, sizeof(RebContext));
    ctx->current_state = REB_STATE_IDLE;
}

/**
 * @brief Constructs a default @c RebInputs structure with safe baseline values.
 * @details Sets @c battery_voltage to 12.0 V, @c timestamp_ms to @c T_INTRUSION,
 *          and @c nonce to 1. All other fields are zero-initialised. Tests
 *          override individual fields as required.
 * @return A @c RebInputs structure with safe default values.
 */
static RebInputs make_inputs(void)
{
    RebInputs in;
    memset(&in, 0, sizeof(in));
    in.battery_voltage = 12.0f;
    in.timestamp_ms    = T_INTRUSION;
    in.nonce           = 1U;
    return in;
}

/**
 * @brief Advances the state machine from IDLE to @c REB_STATE_THEFT_CONFIRMED
 *        by injecting an intrusion-detected event at @c T_INTRUSION.
 * @details Corresponds to the IDLE → THEFT_CONFIRMED transition described in
 *          FR-001 and FR-002 of REB-SRS-001.
 * @param ctx [in,out] Context to be driven to @c REB_STATE_THEFT_CONFIRMED.
 */
static void drive_to_theft_confirmed(RebContext *ctx)
{
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;
    reb_state_machine_step(ctx, &in, &out);
}

/**
 * @brief Advances the state machine from IDLE to @c REB_STATE_BLOCKING by
 *        first calling @c drive_to_theft_confirmed() and then expiring the
 *        60-second theft-confirmation window.
 * @details Corresponds to the THEFT_CONFIRMED → BLOCKING transition described
 *          in FR-001 and FR-003 of REB-SRS-001. The timestamp used is
 *          @c T_CONFIRMED, which equals @c T_INTRUSION + @c REB_THEFT_CONFIRM_WINDOW_MS.
 * @param ctx [in,out] Context to be driven to @c REB_STATE_BLOCKING.
 */
static void drive_to_blocking(RebContext *ctx)
{
    drive_to_theft_confirmed(ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_CONFIRMED;
    reb_state_machine_step(ctx, &in, &out);
}

/**
 * @brief Advances the state machine from IDLE to @c REB_STATE_BLOCKED by
 *        calling @c drive_to_blocking() and then satisfying the 120-second
 *        safe-stop dwell condition.
 * @details Corresponds to the BLOCKING → BLOCKED transition described in
 *          FR-010 and FR-011 of REB-SRS-001. Two steps are required: the first
 *          registers the stop timestamp, and the second fires after the full
 *          dwell period has elapsed.
 * @param ctx [in,out] Context to be driven to @c REB_STATE_BLOCKED.
 */
static void drive_to_blocked(RebContext *ctx)
{
    drive_to_blocking(ctx);
    RebInputs in = make_inputs();
    RebOutputs out;

    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_state_machine_step(ctx, &in, &out);

    in.timestamp_ms = T_BLOCKED_TS;
    reb_state_machine_step(ctx, &in, &out);
}

/**
 * @brief Global nonce sequence counter used by @c prepare_valid_unlock_inputs().
 * @details Incremented relative to @c ctx->last_valid_nonce to produce a nonce
 *          that passes the sliding-window check.
 */
static uint32_t g_nonce_seq = 1U;

/**
 * @brief Fills an @c RebInputs structure with the minimum fields required for
 *        a valid authenticated unlock command.
 * @details Sets @c remote_command to @c REB_REMOTE_UNLOCK, advances the nonce
 *          one step beyond @c ctx->last_valid_nonce, sets @c tcu_ack_received
 *          to @c true, and uses @c T_BLOCKED_TS + 1 as the timestamp. This
 *          helper exercises the unlock path described in SRS §6.2 Recovery
 *          Procedure.
 * @param in  [out] Pointer to the @c RebInputs structure to be populated.
 * @param ctx [in]  Pointer to the current context, used to derive the next
 *                  valid nonce value.
 */
static void prepare_valid_unlock_inputs(RebInputs *in, RebContext *ctx)
{
    g_nonce_seq = ctx->last_valid_nonce + 1U;
    in->remote_command   = REB_REMOTE_UNLOCK;
    in->nonce            = g_nonce_seq;
    in->timestamp_ms     = T_BLOCKED_TS + 1U;
    in->tcu_ack_received = true;
    in->battery_voltage  = 12.0f;
    in->vehicle_speed_kmh = 0.0f;
    in->engine_rpm       = 0U;
}

/*==============================================================================
 * Group 1: reb_state_machine_init
 *==============================================================================*/

/**
 * @brief Verifies that @c reb_state_machine_init() sets @c current_state to
 *        @c REB_STATE_IDLE and clears @c last_valid_nonce, even when called on
 *        a context pre-filled with non-zero data.
 * @test Group 1 — state machine initialisation
 * @see reb_state_machine_init
 */
static void test_init_sets_idle(void)
{
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_state_machine_init(&ctx);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
    TEST_ASSERT_EQUAL_INT(0, ctx.last_valid_nonce);
}

/**
 * @brief Verifies that @c reb_state_machine_init() zeroes every entry in
 *        @c nonce_history[], even when the array was pre-filled with 0xFF.
 * @details A non-zero nonce history would cause legitimate commands to be
 *          rejected by the replay-detection check in @c reb_security.c.
 * @test Group 1 — state machine initialisation
 * @see reb_state_machine_init
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_init_clears_nonce_history(void)
{
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_state_machine_init(&ctx);
    for (int i = 0; i < REB_NONCE_HISTORY_SIZE; i++)
        TEST_ASSERT_EQUAL_INT(0, ctx.nonce_history[i]);
}

/**
 * @brief Verifies that @c reb_state_machine_init() resets
 *        @c invalid_unlock_attempts to zero.
 * @details The brute-force counter must be cleared on a fresh initialisation
 *          so that the lockout threshold is not immediately triggered after a
 *          power cycle without a prior saved state.
 * @test Group 1 — state machine initialisation
 * @see reb_state_machine_init
 */
static void test_init_clears_invalid_unlock_attempts(void)
{
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_state_machine_init(&ctx);
    TEST_ASSERT_EQUAL_INT(0, ctx.invalid_unlock_attempts);
}

/**
 * @brief Verifies that @c reb_state_machine_init() zeroes all timing fields:
 *        @c theft_confirmed_timestamp_ms, @c vehicle_stopped_timestamp_ms, and
 *        @c last_blocked_retx_timestamp_ms.
 * @details Non-zero residual timer values would corrupt the duration checks
 *          performed in the THEFT_CONFIRMED and BLOCKING states.
 * @test Group 1 — state machine initialisation
 * @see reb_state_machine_init
 */
static void test_init_clears_timers(void)
{
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_state_machine_init(&ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.theft_confirmed_timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.vehicle_stopped_timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.last_blocked_retx_timestamp_ms);
}

/**
 * @brief Verifies that @c reb_state_machine_init() resets @c nonce_history_index
 *        to zero, resetting the circular buffer write pointer.
 * @test Group 1 — state machine initialisation
 * @see reb_state_machine_init
 */
static void test_init_clears_nonce_history_index(void)
{
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_state_machine_init(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, ctx.nonce_history_index);
}

/*==============================================================================
 * Group 2: IDLE state
 *==============================================================================*/

/**
 * @brief Verifies that the state machine remains in @c REB_STATE_IDLE when
 *        no intrusion is detected and no remote command is present.
 * @details Corresponds to SC0 of FR-001: system remains in IDLE under normal
 *          conditions.
 * @test Group 2 — IDLE state behaviour
 * @see FR-001 (REB-SRS-001)
 */
static void test_idle_no_event_stays_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = false;
    in.remote_command = REB_REMOTE_NONE;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
    TEST_ASSERT_FALSE(out.visual_alert);
    TEST_ASSERT_FALSE(out.starter_lock);
}

/**
 * @brief Verifies that a physical intrusion event transitions the state machine
 *        from @c REB_STATE_IDLE to @c REB_STATE_THEFT_CONFIRMED and records the
 *        event timestamp in @c theft_confirmed_timestamp_ms.
 * @details Corresponds to SC1 of FR-001 and FR-002 (automatic sensor activation).
 * @test Group 2 — IDLE state behaviour
 * @see FR-001, FR-002, FR-007 (REB-SRS-001)
 */
static void test_idle_intrusion_goes_to_theft_confirmed(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = true;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(T_INTRUSION, ctx.theft_confirmed_timestamp_ms);
}

/**
 * @brief Verifies that a @c REB_REMOTE_BLOCK command with a valid nonce
 *        transitions the state machine from IDLE to THEFT_CONFIRMED.
 * @details The nonce (1) is one step ahead of @c last_valid_nonce (0), placing
 *          it inside the sliding window. Corresponds to FR-005 (remote activation
 *          via 4G/5G) and to the authenticated remote command path of FR-002.
 * @test Group 2 — IDLE state behaviour
 * @see FR-002, FR-005, NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_remote_block_valid_nonce_goes_theft_confirmed(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that a @c REB_REMOTE_BLOCK command with a nonce that is
 *        below @c last_valid_nonce is rejected and the state remains IDLE.
 * @details A nonce of 50 with @c last_valid_nonce = 100 violates the monotonic
 *          ordering requirement of the sliding-window check. Corresponds to SC2
 *          of FR-002 and to NFR-SEC-001 anti-replay requirements.
 * @test Group 2 — IDLE state behaviour
 * @see FR-002, NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_remote_block_invalid_nonce_stays_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 100U;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 50U;
    in.timestamp_ms   = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that a @c REB_REMOTE_BLOCK command with @c timestamp_ms equal
 *        to zero is rejected and the state remains IDLE.
 * @details A zero timestamp fails the freshness check in
 *          @c reb_security_validate_remote_command(). Corresponds to NFR-SEC-001.
 * @test Group 2 — IDLE state behaviour
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_remote_block_timestamp_zero_stays_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = 0U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that a nonce that exceeds @c last_valid_nonce by more than
 *        @c REB_NONCE_WINDOW_SIZE is rejected and the state remains IDLE.
 * @details The nonce @c REB_NONCE_WINDOW_SIZE + 2 falls outside the upper bound
 *          of the valid window. Corresponds to NFR-SEC-001.
 * @test Group 2 — IDLE state behaviour
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_remote_block_nonce_out_of_window_stays_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = REB_NONCE_WINDOW_SIZE + 2U;
    in.timestamp_ms   = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that a nonce exactly equal to @c last_valid_nonce +
 *        @c REB_NONCE_WINDOW_SIZE is accepted and the state transitions to
 *        @c REB_STATE_THEFT_CONFIRMED.
 * @details This is the upper boundary of the valid nonce window; the value must
 *          be accepted. Corresponds to the window-size boundary condition of
 *          NFR-SEC-001.
 * @test Group 2 — IDLE state behaviour
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_remote_block_nonce_boundary_exact_window(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = REB_NONCE_WINDOW_SIZE;
    in.timestamp_ms   = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that a @c REB_REMOTE_CANCEL command in the IDLE state has no
 *        effect and the state remains IDLE.
 * @details The IDLE state handler does not process CANCEL commands; the state
 *          machine should remain stable.
 * @test Group 2 — IDLE state behaviour
 */
static void test_idle_cancel_command_stays_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_CANCEL;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that a @c REB_REMOTE_UNLOCK command in the IDLE state has no
 *        effect and the state remains IDLE.
 * @details According to FR-002, only authorised theft confirmation sources can
 *          trigger the IDLE → THEFT_CONFIRMED transition. An UNLOCK command in
 *          IDLE has no handler and must be ignored.
 * @test Group 2 — IDLE state behaviour
 * @see FR-002 (REB-SRS-001)
 */
static void test_idle_unlock_command_stays_idle(void)
{
    /* FR-002: only authorised sources can exit IDLE.
       UNLOCK in IDLE has no handler - state must remain IDLE. */
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_UNLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that all output fields are deasserted when the state machine
 *        steps in the IDLE state with no active event.
 * @details Confirms that @c visual_alert, @c acoustic_alert, @c starter_lock,
 *          @c send_status_to_tcu, and @c derate_percent are all at their safe
 *          default (inactive/zero) values during normal IDLE operation.
 * @test Group 2 — IDLE state behaviour
 */
static void test_idle_outputs_all_false(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.visual_alert);
    TEST_ASSERT_FALSE(out.acoustic_alert);
    TEST_ASSERT_FALSE(out.starter_lock);
    TEST_ASSERT_FALSE(out.send_status_to_tcu);
    TEST_ASSERT_EQUAL_UINT8(0U, out.derate_percent);
}

/*==============================================================================
 * Group 3: THEFT_CONFIRMED state
 *==============================================================================*/

/**
 * @brief Verifies that @c visual_alert and @c acoustic_alert are both asserted
 *        on each step while the state machine is in @c REB_STATE_THEFT_CONFIRMED.
 * @details Corresponds to the alert activation described in SRS §6.1 for the
 *          THEFT_CONFIRMED state and FR-013.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-013 (REB-SRS-001)
 */
static void test_theft_confirmed_outputs_alerts(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_INTRUSION + 1000U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_TRUE(out.visual_alert);
    TEST_ASSERT_TRUE(out.acoustic_alert);
}

/**
 * @brief Verifies that @c starter_lock is NOT asserted while the state machine
 *        is in @c REB_STATE_THEFT_CONFIRMED.
 * @details According to NFR-SAF-001 and FR-003 (AC0), the starter lock must
 *          never be engaged during the confirmation window — only progressive
 *          fuel reduction is permitted before the vehicle is confirmed stopped.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-003, NFR-SAF-001 (REB-SRS-001)
 */
static void test_theft_confirmed_no_starter_lock(void)
{
    /* FR-003 AC0: vehicle moving - ignition blocking must be rejected */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_INTRUSION + 1000U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.starter_lock);
}

/**
 * @brief Verifies that a @c REB_REMOTE_CANCEL command in the
 *        @c REB_STATE_THEFT_CONFIRMED state returns the system to
 *        @c REB_STATE_IDLE.
 * @details Models the cancellation scenario described in SRS §6.1 and in the
 *          operational scenario 7.1.2: the owner cancels within the 60-second
 *          window. Corresponds to FR-008.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-008 (REB-SRS-001)
 */
static void test_theft_confirmed_cancel_returns_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_CANCEL;
    in.timestamp_ms   = T_INTRUSION + 1000U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that the state machine remains in @c REB_STATE_THEFT_CONFIRMED
 *        when the timestamp has not yet reached the confirmation window boundary.
 * @details The timestamp @c T_INTRUSION + REB_THEFT_CONFIRM_WINDOW_MS - 1 ms is
 *          one millisecond before the window expires. The transition to BLOCKING
 *          must not occur.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-001, FR-008 (REB-SRS-001)
 */
static void test_theft_confirmed_window_not_expired_stays(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_INTRUSION + REB_THEFT_CONFIRM_WINDOW_MS - 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that the state machine transitions to @c REB_STATE_BLOCKING
 *        exactly when the theft-confirmation window expires.
 * @details The timestamp @c T_CONFIRMED is exactly equal to
 *          @c T_INTRUSION + @c REB_THEFT_CONFIRM_WINDOW_MS, which is the
 *          boundary at which the transition must fire. Corresponds to SC2 of
 *          FR-001.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-001, FR-008 (REB-SRS-001)
 */
static void test_theft_confirmed_window_expired_goes_blocking(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_CONFIRMED;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
}

/**
 * @brief Verifies that the transition to @c REB_STATE_BLOCKING also occurs when
 *        the timestamp is one millisecond past the window boundary.
 * @details Confirms that the elapsed-time check uses a >= comparison, so any
 *          timestamp beyond @c T_CONFIRMED also triggers the transition.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-001, FR-008 (REB-SRS-001)
 */
static void test_theft_confirmed_window_expired_plus_one(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_CONFIRMED + 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
}

/**
 * @brief Verifies that receiving a second @c REB_REMOTE_BLOCK command while
 *        already in @c REB_STATE_THEFT_CONFIRMED does not reset the confirmation
 *        timestamp.
 * @details The THEFT_CONFIRMED state has no handler for BLOCK commands; a second
 *          BLOCK must be silently ignored. The original timestamp must be
 *          preserved so that the 60-second window continues from the first event.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-002 (REB-SRS-001)
 */
static void test_theft_confirmed_block_command_does_not_retrip(void)
{
    /* FR-002: a second BLOCK command in THEFT_CONFIRMED must not reset the timer. */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);
    uint32_t original_ts = ctx.theft_confirmed_timestamp_ms;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = ctx.last_valid_nonce + 1U;
    in.timestamp_ms   = T_INTRUSION + 500U;
    reb_state_machine_step(&ctx, &in, &out);
    /* Still in THEFT_CONFIRMED (window not expired), timestamp unchanged */
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(original_ts, ctx.theft_confirmed_timestamp_ms);
}

/**
 * @brief Verifies that the confirmation window is exactly 60 seconds
 *        (60 000 ms), as required by SRS §6.1 and FR-008.
 * @details Both the value of @c REB_THEFT_CONFIRM_WINDOW_MS and the resulting
 *          state transition are checked at t = T_INTRUSION + 60 000 ms.
 * @test Group 3 — THEFT_CONFIRMED state behaviour
 * @see FR-008, SRS §6.1 (REB-SRS-001)
 */
static void test_theft_confirmed_window_exactly_60s(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_theft_confirmed(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    /* exactly at 60 000 ms */
    in.timestamp_ms = T_INTRUSION + 60000U;
    TEST_ASSERT_EQUAL_UINT32(60000U, REB_THEFT_CONFIRM_WINDOW_MS);
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
}

/*==============================================================================
 * Group 4: BLOCKING state
 *==============================================================================*/

/**
 * @brief Verifies that the state machine remains in @c REB_STATE_BLOCKING and
 *        applies a non-zero derate value when the vehicle speed is above
 *        @c REB_MAX_ALLOWED_SPEED_FOR_LOCK.
 * @details Corresponds to the in-motion progressive fuel-reduction requirement
 *          in FR-009 and NFR-SAF-001: the system must not engage starter lock
 *          while moving.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-009, NFR-SAF-001 (REB-SRS-001)
 */
static void test_blocking_vehicle_moving_applies_derating(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = REB_MAX_ALLOWED_SPEED_FOR_LOCK + 1.0f;
    in.timestamp_ms      = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
    TEST_ASSERT_GREATER_THAN(0U, out.derate_percent);
}

/**
 * @brief Verifies that when the vehicle speed is below
 *        @c REB_MAX_ALLOWED_SPEED_FOR_LOCK, the derating output is set to
 *        @c REB_DERATE_MAX_PERCENT rather than being progressively incremented.
 * @details At low speed the system enters the stopped-vehicle branch and
 *          applies maximum derating to prevent engine restart, as required by
 *          FR-011.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-011 (REB-SRS-001)
 */
static void test_blocking_vehicle_slow_derating_sets_derate_step(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.3f;
    in.timestamp_ms      = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_MAX_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that at a speed well above @c REB_SAFE_MOVING_SPEED_KMH,
 *        @c reb_apply_derating() increments the derating by exactly one step
 *        (@c REB_DERATE_STEP_PERCENT) on the first call.
 * @details Exercises the incremental branch of @c reb_apply_derating() from a
 *          zero initial derating, as required by FR-009.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-009 (REB-SRS-001)
 */
static void test_blocking_vehicle_moving_fast_derating_step_up(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 6.0f;
    in.timestamp_ms      = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_STEP_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that the first step where vehicle speed falls below
 *        @c REB_MAX_ALLOWED_SPEED_FOR_LOCK records the stop timestamp in
 *        @c vehicle_stopped_timestamp_ms.
 * @details The stop timestamp is the reference for the 120-second dwell timer
 *          defined in FR-010 and FR-011.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_blocking_vehicle_slow_first_stop_sets_timestamp(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    ctx.vehicle_stopped_timestamp_ms = 0U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_UINT32(T_CONFIRMED + 100U, ctx.vehicle_stopped_timestamp_ms);
}

/**
 * @brief Verifies that the state machine remains in @c REB_STATE_BLOCKING when
 *        the vehicle has been stopped but the 120-second dwell timer has not yet
 *        elapsed.
 * @details Uses a timestamp one millisecond short of @c REB_STOP_HOLD_TIME_MS
 *          after the stop event to confirm that the transition to BLOCKED does
 *          not fire prematurely. Corresponds to FR-010.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_blocking_stopped_hold_not_complete_stays(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);

    in.timestamp_ms = T_CONFIRMED + 100U + REB_STOP_HOLD_TIME_MS - 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
}

/**
 * @brief Verifies that the state machine transitions to @c REB_STATE_BLOCKED
 *        when the vehicle has remained stopped for exactly @c REB_STOP_HOLD_TIME_MS.
 * @details Exercises the boundary at which the 120-second dwell fires and the
 *          BLOCKING → BLOCKED transition occurs, as required by FR-011.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_blocking_stopped_hold_complete_goes_blocked(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);

    in.timestamp_ms = T_CONFIRMED + 100U + REB_STOP_HOLD_TIME_MS;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/**
 * @brief Verifies that @c vehicle_stopped_timestamp_ms is not overwritten on
 *        subsequent steps where the vehicle remains stopped.
 * @details Once the stop timestamp is set, it must be preserved unchanged so
 *          that the elapsed-time calculation in subsequent steps remains correct.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_blocking_already_stopped_does_not_reset_timestamp(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);

    uint32_t first_stop_ts = ctx.vehicle_stopped_timestamp_ms;

    in.timestamp_ms = T_CONFIRMED + 200U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_UINT32(first_stop_ts, ctx.vehicle_stopped_timestamp_ms);
}

/**
 * @brief Verifies that @c vehicle_stopped_timestamp_ms is reset to zero when
 *        the vehicle starts moving again after having stopped.
 * @details The dwell timer must restart from zero whenever the speed rises above
 *          @c REB_MAX_ALLOWED_SPEED_FOR_LOCK, as defined in FR-011.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-011 (REB-SRS-001)
 */
static void test_blocking_moving_resets_stopped_timestamp(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);

    in.vehicle_speed_kmh = 2.0f;
    in.timestamp_ms = T_CONFIRMED + 200U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.vehicle_stopped_timestamp_ms);
}

/**
 * @brief Verifies that @c reb_apply_derating() increments @c derate_percent
 *        by @c REB_DERATE_STEP_PERCENT when vehicle speed exceeds
 *        @c REB_SAFE_MOVING_SPEED_KMH.
 * @details Directly exercises the incremental branch of the derating helper.
 *          FR-009 requires at least one step of reduction per control cycle
 *          while the vehicle is moving above the safe-moving threshold.
 * @test Group 4 — BLOCKING state behaviour / derating
 * @see FR-009 (REB-SRS-001)
 */
static void test_apply_derating_speed_above_safe_increments(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = REB_SAFE_MOVING_SPEED_KMH + 1.0f;
    in.timestamp_ms = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_STEP_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that @c reb_apply_derating() sets @c derate_percent to
 *        @c REB_DERATE_MAX_PERCENT when vehicle speed is at or below
 *        @c REB_MAX_ALLOWED_SPEED_FOR_LOCK.
 * @details The stopped-vehicle branch of the BLOCKING state bypasses the
 *          incremental ramp and applies maximum derating immediately, as
 *          required by FR-011.
 * @test Group 4 — BLOCKING state behaviour / derating
 * @see FR-011 (REB-SRS-001)
 */
static void test_apply_derating_speed_below_safe_sets_min(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_MAX_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that both @c visual_alert and @c acoustic_alert are asserted
 *        on every step while the state machine is in @c REB_STATE_BLOCKING.
 * @details Alert activation in the BLOCKING state is required by FR-013 and
 *          SRS §6.1.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-013, SRS §6.1 (REB-SRS-001)
 */
static void test_blocking_alerts_always_active(void)
{
    /* FR-013: visual and acoustic alerts must be active during BLOCKING */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 3.0f;
    in.timestamp_ms      = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_TRUE(out.visual_alert);
    TEST_ASSERT_TRUE(out.acoustic_alert);
}

/**
 * @brief Verifies that @c starter_lock is NOT asserted while the vehicle is
 *        moving in the @c REB_STATE_BLOCKING state.
 * @details NFR-SAF-001 mandates that the starter motor block is prohibited
 *          at any vehicle speed above zero. Tested at 80 km/h.
 * @test Group 4 — BLOCKING state behaviour
 * @see NFR-SAF-001 (REB-SRS-001)
 */
static void test_blocking_no_starter_lock_while_moving(void)
{
    /* NFR-SAF-001: no starter lock while vehicle speed > 0 */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 80.0f;
    in.timestamp_ms      = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.starter_lock);
}

/**
 * @brief Verifies that a speed exactly equal to @c REB_MAX_ALLOWED_SPEED_FOR_LOCK
 *        enters the stopped branch and applies @c REB_DERATE_MAX_PERCENT.
 * @details The boundary condition: speed <= MAX_ALLOWED (not strictly less than)
 *          must enter the stopped-vehicle path and begin the dwell timer.
 * @test Group 4 — BLOCKING state behaviour / boundary
 * @see FR-010, FR-011, NFR-SAF-001 (REB-SRS-001)
 */
static void test_blocking_speed_exactly_at_lock_limit_stops(void)
{
    /* Boundary: speed == REB_MAX_ALLOWED_SPEED_FOR_LOCK enters stopped branch */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = REB_MAX_ALLOWED_SPEED_FOR_LOCK;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_MAX_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that at a speed between @c REB_MAX_ALLOWED_SPEED_FOR_LOCK and
 *        @c REB_SAFE_MOVING_SPEED_KMH, the output derating is set to
 *        @c REB_DERATE_MIN_PERCENT.
 * @details At 2 km/h (> 0.5 m/s but <= 5.0 km/h), @c reb_apply_derating()
 *          selects the minimum-derating branch rather than the incremental one.
 * @test Group 4 — BLOCKING state behaviour / derating
 * @see FR-009 (REB-SRS-001)
 */
static void test_blocking_speed_between_safe_and_lock(void)
{
    /* speed > MAX_ALLOWED (0.5) but <= SAFE (5.0): reb_apply_derating -> MIN */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 2.0f; /* > 0.5 && <= 5.0 */
    in.timestamp_ms      = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_MIN_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that a @c REB_REMOTE_CANCEL command in the BLOCKING state is
 *        silently ignored and the state remains @c REB_STATE_BLOCKING.
 * @details FR-003 defines the valid transitions out of BLOCKING; CANCEL is not
 *          among them. The state machine must discard the command.
 * @test Group 4 — BLOCKING state behaviour
 * @see FR-003 (REB-SRS-001)
 */
static void test_blocking_cancel_ignored(void)
{
    /* FR-003: BLOCKING state has no CANCEL handler - state must remain BLOCKING */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command    = REB_REMOTE_CANCEL;
    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms      = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
}

/**
 * @brief Verifies that the BLOCKING → BLOCKED transition fires exactly when the
 *        safe-stop dwell reaches 120 seconds (@c REB_STOP_HOLD_TIME_MS).
 * @details Boundary test for the dwell timer: the transition must occur at
 *          t_stop + 120 000 ms and not one millisecond earlier. Corresponds to
 *          FR-010 and FR-011.
 * @test Group 4 — BLOCKING state behaviour / boundary
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_blocking_stop_hold_exactly_120s(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 0.0f;
    uint32_t t_stop = T_CONFIRMED + 200U;
    in.timestamp_ms = t_stop;
    reb_state_machine_step(&ctx, &in, &out); /* registers stop */

    in.timestamp_ms = t_stop + 120000U; /* exactly 120 s = REB_STOP_HOLD_TIME_MS */
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/*==============================================================================
 * Group 5: BLOCKED state
 *==============================================================================*/

/**
 * @brief Verifies that @c starter_lock, @c visual_alert, and
 *        @c derate_percent are all set to their immobilised values on every step
 *        in the @c REB_STATE_BLOCKED state.
 * @details Once BLOCKED, the starter motor must be permanently inhibited until
 *          an authenticated unlock is received. Corresponds to SRS §6.1 BLOCKED
 *          state description and FR-011.
 * @test Group 5 — BLOCKED state behaviour
 * @see FR-011, SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_starter_lock_always_set(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_BLOCKED_TS + 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_TRUE(out.starter_lock);
    TEST_ASSERT_EQUAL_INT(REB_DERATE_MAX_PERCENT, out.derate_percent);
    TEST_ASSERT_TRUE(out.visual_alert);
}

/**
 * @brief Verifies that @c acoustic_alert is NOT asserted in the
 *        @c REB_STATE_BLOCKED state.
 * @details The BLOCKED state activates only the visual alert, not the acoustic
 *          one. This is consistent with the SRS §6.1 BLOCKED state description,
 *          which does not list acoustic alerts as an output.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_no_acoustic_alert(void)
{
    /* BLOCKED state: only visual_alert, not acoustic_alert */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.timestamp_ms = T_BLOCKED_TS + 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.acoustic_alert);
}

/**
 * @brief Verifies that @c send_status_to_tcu is asserted when the elapsed time
 *        since the last retransmission exceeds @c REB_BLOCKED_RETRANSMIT_MS.
 * @details The BLOCKED state must periodically retransmit immobilisation status
 *          to the TCU every 5 seconds, as described in SRS §6.1.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_retransmit_on_interval(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    ctx.last_blocked_retx_timestamp_ms = 0U;
    in.timestamp_ms = REB_BLOCKED_RETRANSMIT_MS + 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_TRUE(out.send_status_to_tcu);
}

/**
 * @brief Verifies that @c send_status_to_tcu is NOT asserted before the
 *        retransmission interval has elapsed.
 * @details The retransmission must not fire until at least
 *          @c REB_BLOCKED_RETRANSMIT_MS milliseconds have passed since the last
 *          transmission.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_no_retransmit_before_interval(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    ctx.last_blocked_retx_timestamp_ms = T_BLOCKED_TS + 1U;
    in.timestamp_ms = T_BLOCKED_TS + 1U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.send_status_to_tcu);
}

/**
 * @brief Verifies that @c last_blocked_retx_timestamp_ms is updated to the
 *        current input timestamp when a retransmission fires.
 * @details The timestamp update ensures that the retransmission interval is
 *          measured from the last actual transmission, not from a fixed base.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_retransmit_updates_timestamp(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    ctx.last_blocked_retx_timestamp_ms = 0U;
    in.timestamp_ms = T_BLOCKED_TS + REB_BLOCKED_RETRANSMIT_MS;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_UINT32(T_BLOCKED_TS + REB_BLOCKED_RETRANSMIT_MS,
                             ctx.last_blocked_retx_timestamp_ms);
}

/**
 * @brief Verifies that the retransmission fires at exactly 5 seconds
 *        (@c REB_BLOCKED_RETRANSMIT_MS) after the last recorded transmission.
 * @details Boundary test for the 5-second retransmission interval described in
 *          SRS §6.1.
 * @test Group 5 — BLOCKED state behaviour / boundary
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_retransmit_at_exact_5s_interval(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    ctx.last_blocked_retx_timestamp_ms = 10000U;
    in.timestamp_ms = 10000U + 5000U; /* exactly 5 s */
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_TRUE(out.send_status_to_tcu);
}

/**
 * @brief Verifies that a valid authenticated unlock command transitions the
 *        state machine from @c REB_STATE_BLOCKED back to @c REB_STATE_IDLE.
 * @details Models the recovery procedure described in SRS §6.2: a valid remote
 *          unlock with TCU acknowledgement must release the immobilisation.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.2 (REB-SRS-001)
 */
static void test_blocked_valid_unlock_returns_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    prepare_valid_unlock_inputs(&in, &ctx);
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that an invalid unlock command (nonce = 0, no TCU ACK)
 *        increments @c invalid_unlock_attempts and leaves the state machine in
 *        @c REB_STATE_BLOCKED.
 * @details Both @c reb_security_unlock_allowed() and the BLOCKED state handler
 *          each increment the counter on failure, resulting in a total increment
 *          of 2 per rejected attempt.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.2 (REB-SRS-001)
 */
static void test_blocked_invalid_unlock_increments_counter(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 0U;
    in.timestamp_ms      = T_BLOCKED_TS + 1U;
    in.tcu_ack_received  = false;
    in.battery_voltage   = 12.0f;

    uint8_t before = ctx.invalid_unlock_attempts;
    reb_state_machine_step(&ctx, &in, &out);
    /* unlock_allowed increments +1, state machine increments +1 -> total +2 */
    TEST_ASSERT_EQUAL_INT(before + 2U, ctx.invalid_unlock_attempts);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/**
 * @brief Verifies that an unlock command with a valid nonce but without TCU
 *        acknowledgement is rejected and the state remains @c REB_STATE_BLOCKED.
 * @details TCU confirmation is a mandatory secondary authorisation condition for
 *          the unlock path, as defined in SRS §6.2.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.2 (REB-SRS-001)
 */
static void test_blocked_unlock_no_tcu_ack_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    prepare_valid_unlock_inputs(&in, &ctx);
    in.tcu_ack_received = false;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/**
 * @brief Verifies that multiple consecutive invalid unlock attempts cause
 *        @c invalid_unlock_attempts to accumulate across calls.
 * @details After three rejected attempts the counter must be at least 3,
 *          consistent with the lockout threshold @c REB_MAX_INVALID_ATTEMPTS
 *          defined in @c reb_config.h and the recovery procedure in SRS §6.2.
 * @test Group 5 — BLOCKED state behaviour
 * @see SRS §6.2, REB_MAX_INVALID_ATTEMPTS (REB-SRS-001)
 */
static void test_blocked_multiple_invalid_unlocks_accumulate(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command   = REB_REMOTE_UNLOCK;
    in.nonce            = 0U;
    in.timestamp_ms     = T_BLOCKED_TS + 1U;
    in.tcu_ack_received = false;

    for (int i = 0; i < 3; i++)
        reb_state_machine_step(&ctx, &in, &out);

    TEST_ASSERT_GREATER_OR_EQUAL(3U, ctx.invalid_unlock_attempts);
}

/*==============================================================================
 * Group 6: Default (invalid state)
 *==============================================================================*/

/**
 * @brief Verifies that an unrecognised state value (99) causes the state machine
 *        to reset to @c REB_STATE_IDLE via the default case handler.
 * @details The default branch of @c reb_state_machine_step() must call
 *          @c reb_state_machine_init() and log an error when an invalid state
 *          is encountered.
 * @test Group 6 — default / invalid state handling
 * @see reb_state_machine_step
 */
static void test_invalid_state_resets_to_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.current_state = (RebState)99;

    RebInputs in = make_inputs();
    RebOutputs out;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that the maximum invalid state value (255) also causes the
 *        state machine to reset to @c REB_STATE_IDLE.
 * @details Complements @c test_invalid_state_resets_to_idle() with a different
 *          out-of-range value to ensure the default case is not sensitive to
 *          the specific invalid value.
 * @test Group 6 — default / invalid state handling
 * @see reb_state_machine_step
 */
static void test_invalid_state_255_resets_to_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.current_state = (RebState)255;

    RebInputs in = make_inputs();
    RebOutputs out;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/*==============================================================================
 * Group 7: reb_security_validate_remote_command
 *==============================================================================*/

/**
 * @brief Verifies that a command with a valid nonce (inside the sliding window,
 *        not replayed) is accepted and @c last_valid_nonce is updated.
 * @details Nonce 15 with @c last_valid_nonce = 10 is within the window
 *          (@c REB_NONCE_WINDOW_SIZE = 32). Corresponds to NFR-SEC-001.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_valid_nonce_passes(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 10U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 15U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
    TEST_ASSERT_EQUAL_UINT32(15U, ctx.last_valid_nonce);
}

/**
 * @brief Verifies that a nonce equal to @c last_valid_nonce is rejected.
 * @details The sliding-window check requires nonce > last_valid_nonce. An equal
 *          value fails the monotonic ordering condition. Corresponds to
 *          NFR-SEC-001.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_equal_last_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 10U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 10U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a previously accepted nonce is rejected on a second
 *        presentation (replay attack).
 * @details The same nonce (5) is submitted twice. The first call succeeds and
 *          stores the nonce in history. The second call must be rejected by the
 *          replay-detection check. Corresponds to NFR-SEC-001.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_replay_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 5U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
    ctx.last_valid_nonce = 0U; /* allow nonce 5 to be in window again */
    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a command with @c REB_REMOTE_NONE is rejected regardless
 *        of nonce and timestamp validity.
 * @details @c REB_REMOTE_NONE indicates the absence of a command and must never
 *          pass validation.
 * @test Group 7 — remote command validation
 * @see reb_security_validate_remote_command
 */
static void test_security_command_none_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_NONE;
    in.nonce          = 1U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a command with a zero timestamp is rejected.
 * @details A zero timestamp is treated as invalid by the freshness check in
 *          @c reb_security_validate_remote_command(). Corresponds to NFR-SEC-001.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_timestamp_zero_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = 0U;

    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a nonce exceeding the window upper bound by one is
 *        rejected.
 * @details Nonce @c REB_NONCE_WINDOW_SIZE + 1 is outside the valid range
 *          (1 .. @c REB_NONCE_WINDOW_SIZE above @c last_valid_nonce). Corresponds
 *          to NFR-SEC-001.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_out_of_window_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = REB_NONCE_WINDOW_SIZE + 1U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a nonce exactly at the upper bound of the window
 *        (@c last_valid_nonce + @c REB_NONCE_WINDOW_SIZE) is accepted.
 * @details This is the maximum valid nonce value relative to the last accepted
 *          nonce. The check uses <=, so the boundary value must pass.
 * @test Group 7 — remote command validation / boundary
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_exactly_window_size_accepted(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = REB_NONCE_WINDOW_SIZE;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a nonce at @c REB_NONCE_WINDOW_SIZE + 1 above
 *        @c last_valid_nonce is rejected (above upper bound).
 * @details Companion to @c test_security_nonce_exactly_window_size_accepted():
 *          one step beyond the boundary must be rejected.
 * @test Group 7 — remote command validation / boundary
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_window_size_plus_one_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = REB_NONCE_WINDOW_SIZE + 1U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that the circular nonce-history buffer wraps correctly after
 *        @c REB_NONCE_HISTORY_SIZE validations, resetting @c nonce_history_index
 *        to zero.
 * @details After @c REB_NONCE_HISTORY_SIZE successful validations, the write
 *          index must return to slot 0, confirming correct circular-buffer
 *          behaviour. Corresponds to NFR-SEC-001.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_history_circular_overflow(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.timestamp_ms   = 1000U;

    for (uint32_t i = 1U; i <= (uint32_t)REB_NONCE_HISTORY_SIZE; i++)
    {
        in.nonce = ctx.last_valid_nonce + 1U;
        reb_security_validate_remote_command(&in, &ctx);
    }
    TEST_ASSERT_EQUAL_INT(0, ctx.nonce_history_index);
}

/**
 * @brief Verifies that a successfully validated nonce is written into the
 *        @c nonce_history[] circular buffer.
 * @details After calling @c reb_security_validate_remote_command() with nonce 5,
 *          at least one slot in @c nonce_history[] must contain the value 5.
 *          This confirms that replay detection will work for subsequent attempts
 *          with the same nonce.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_stored_in_history(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 5U;
    in.timestamp_ms   = 1000U;

    reb_security_validate_remote_command(&in, &ctx);
    bool found = false;
    for (int i = 0; i < REB_NONCE_HISTORY_SIZE; i++)
        if (ctx.nonce_history[i] == 5U) { found = true; break; }
    TEST_ASSERT_TRUE(found);
}

/**
 * @brief Verifies that five sequential nonces, each one step ahead of the
 *        previous, are all accepted in successive calls.
 * @details Exercises the normal command flow in which a remote operator sends
 *          multiple authenticated commands with monotonically increasing nonces.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_multiple_sequential_nonces_accepted(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.timestamp_ms   = 1000U;

    for (uint32_t i = 1U; i <= 5U; i++)
    {
        in.nonce = ctx.last_valid_nonce + 1U;
        TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
    }
}

/**
 * @brief Verifies that a nonce exactly one step above @c last_valid_nonce is
 *        accepted (minimum valid increment).
 * @details nonce = last + 1 is the smallest value that satisfies
 *          nonce > last_valid_nonce. This is the lower boundary of the valid
 *          window.
 * @test Group 7 — remote command validation / boundary
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_exactly_one_ahead_accepted(void)
{
    /* Minimum valid increment: nonce = last + 1 */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 50U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 51U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a nonce at @c last_valid_nonce + @c REB_NONCE_WINDOW_SIZE - 1
 *        is accepted (one step below the upper window boundary).
 * @details Exercises a value within the window that is not the exact boundary,
 *          confirming correct range checking.
 * @test Group 7 — remote command validation / boundary
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_window_minus_one_accepted(void)
{
    /* nonce = last + (WINDOW_SIZE - 1) - still inside window */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 100U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 100U + REB_NONCE_WINDOW_SIZE - 1U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a @c REB_REMOTE_CANCEL command with a valid nonce and
 *        timestamp is accepted by @c reb_security_validate_remote_command().
 * @details CANCEL is a non-NONE command type; the security module treats it as
 *          a valid command to authenticate. The caller (state machine) decides
 *          how to act on it.
 * @test Group 7 — remote command validation
 * @see reb_security_validate_remote_command
 */
static void test_security_cancel_command_validates(void)
{
    /* CANCEL is not NONE, so it can pass validation when nonce/timestamp are valid */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_CANCEL;
    in.nonce          = 1U;
    in.timestamp_ms   = 1000U;

    /* CANCEL is a valid non-NONE command and should pass validation */
    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that @c last_valid_nonce is updated to the incoming nonce
 *        value after a successful validation.
 * @details The updated @c last_valid_nonce forms the reference point for
 *          subsequent sliding-window checks.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_updates_last_valid_nonce_on_success(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 20U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_UNLOCK;
    in.nonce          = 25U;
    in.timestamp_ms   = 1000U;

    reb_security_validate_remote_command(&in, &ctx);
    TEST_ASSERT_EQUAL_UINT32(25U, ctx.last_valid_nonce);
}

/**
 * @brief Verifies that @c last_valid_nonce is NOT modified when validation fails.
 * @details A rejected command must not alter the security context state. The
 *          nonce value 10 is below @c last_valid_nonce = 20 and must be rejected
 *          without side effects.
 * @test Group 7 — remote command validation
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_does_not_update_nonce_on_failure(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 20U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 10U; /* < last_valid_nonce - rejected */
    in.timestamp_ms   = 1000U;

    reb_security_validate_remote_command(&in, &ctx);
    TEST_ASSERT_EQUAL_UINT32(20U, ctx.last_valid_nonce); /* unchanged */
}

/**
 * @brief Verifies that nonce 0 pre-loaded in @c nonce_history[] does not
 *        cause a legitimate nonce 1 to be incorrectly rejected as a replay.
 * @details Nonce 0 appears in history at slot 0 from zero-initialisation.
 *          A fresh command with nonce 1 must not be blocked by this residual
 *          value.
 * @test Group 7 — remote command validation / boundary
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_zero_in_history_is_replay(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.nonce_history[0] = 0U;
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = 1000U;

    /* nonce 0 is in history, but nonce 1 is not - should accept nonce 1 */
    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/*==============================================================================
 * Group 8: reb_security_unlock_allowed
 *==============================================================================*/

/**
 * @brief Verifies that @c reb_security_unlock_allowed() returns @c true when
 *        the command passes validation and TCU acknowledgement is present.
 * @details Models the successful unlock path from SRS §6.2: both the
 *          cryptographic check and the TCU confirmation must succeed.
 * @test Group 8 — unlock authorisation
 * @see reb_security_unlock_allowed, SRS §6.2 (REB-SRS-001)
 */
static void test_unlock_allowed_valid_command_and_ack(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 5U;
    in.timestamp_ms      = 1000U;
    in.tcu_ack_received  = true;

    TEST_ASSERT_TRUE(reb_security_unlock_allowed(&in, &ctx));
}

/**
 * @brief Verifies that @c reb_security_unlock_allowed() returns @c false and
 *        increments @c invalid_unlock_attempts when the command fails
 *        validation (nonce outside window).
 * @details The nonce 5 with @c last_valid_nonce = 100 is below the window lower
 *          bound, causing @c reb_security_validate_remote_command() to fail.
 * @test Group 8 — unlock authorisation
 * @see reb_security_unlock_allowed, SRS §6.2 (REB-SRS-001)
 */
static void test_unlock_allowed_invalid_command_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 100U;

    RebInputs in = make_inputs();
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 5U;
    in.timestamp_ms      = 1000U;
    in.tcu_ack_received  = true;

    uint8_t before = ctx.invalid_unlock_attempts;
    TEST_ASSERT_FALSE(reb_security_unlock_allowed(&in, &ctx));
    TEST_ASSERT_EQUAL_INT(before + 1U, ctx.invalid_unlock_attempts);
}

/**
 * @brief Verifies that @c reb_security_unlock_allowed() returns @c false when
 *        the command is cryptographically valid but TCU acknowledgement is absent.
 * @details TCU confirmation is a mandatory secondary condition for unlock
 *          authorisation, as defined in SRS §6.2.
 * @test Group 8 — unlock authorisation
 * @see reb_security_unlock_allowed, SRS §6.2 (REB-SRS-001)
 */
static void test_unlock_allowed_no_tcu_ack_rejected(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 5U;
    in.timestamp_ms      = 1000U;
    in.tcu_ack_received  = false;

    TEST_ASSERT_FALSE(reb_security_unlock_allowed(&in, &ctx));
}

/**
 * @brief Verifies that @c reb_security_unlock_allowed() returns @c false when
 *        both the command validation and the TCU acknowledgement check fail.
 * @test Group 8 — unlock authorisation
 * @see reb_security_unlock_allowed (REB-SRS-001)
 */
static void test_unlock_allowed_invalid_and_no_ack(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 100U;

    RebInputs in = make_inputs();
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 5U;
    in.timestamp_ms      = 1000U;
    in.tcu_ack_received  = false;

    TEST_ASSERT_FALSE(reb_security_unlock_allowed(&in, &ctx));
}

/**
 * @brief Verifies that @c reb_security_unlock_allowed() increments
 *        @c invalid_unlock_attempts when the command fails the cryptographic
 *        check, even if TCU ACK would be present.
 * @test Group 8 — unlock authorisation
 * @see reb_security_unlock_allowed, SRS §6.2 (REB-SRS-001)
 */
static void test_unlock_allowed_increments_invalid_counter(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 100U;

    RebInputs in = make_inputs();
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 5U;
    in.timestamp_ms      = 1000U;
    in.tcu_ack_received  = true;

    uint8_t before = ctx.invalid_unlock_attempts;
    reb_security_unlock_allowed(&in, &ctx);
    TEST_ASSERT_EQUAL_INT(before + 1U, ctx.invalid_unlock_attempts);
}

/*==============================================================================
 * Group 9: reb_rules_safe_to_block
 *==============================================================================*/

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c true when both
 *        vehicle speed and engine RPM are below their respective limits.
 * @test Group 9 — blocking safety rules
 * @see reb_rules_safe_to_block
 */
static void test_rules_safe_speed_ok_rpm_ok(void)
{
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = REB_MAX_SPEED_FOR_BLOCK_KMH - 1.0f;
    in.engine_rpm        = REB_ENGINE_RPM_LIMIT - 1U;
    TEST_ASSERT_TRUE(reb_rules_safe_to_block(&in));
}

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c true when RPM
 *        is exactly at @c REB_ENGINE_RPM_LIMIT (boundary inclusive).
 * @test Group 9 — blocking safety rules / boundary
 * @see reb_rules_safe_to_block
 */
static void test_rules_safe_speed_ok_rpm_exact_limit(void)
{
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = REB_MAX_SPEED_FOR_BLOCK_KMH - 1.0f;
    in.engine_rpm        = REB_ENGINE_RPM_LIMIT;
    TEST_ASSERT_TRUE(reb_rules_safe_to_block(&in));
}

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c false when
 *        vehicle speed exceeds @c REB_MAX_SPEED_FOR_BLOCK_KMH.
 * @details MC/DC: C1 (speed <= max) = false makes the whole expression false
 *          regardless of C2. Supports NFR-SAF-001: blocking must be denied if
 *          the vehicle is above the safe-stop threshold.
 * @test Group 9 — blocking safety rules / MC/DC
 * @see reb_rules_safe_to_block, NFR-SAF-001 (REB-SRS-001)
 */
static void test_rules_safe_speed_too_high(void)
{
    /* MC/DC: C1 (speed<=max) = false -> false regardless of C2 */
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = REB_MAX_SPEED_FOR_BLOCK_KMH + 1.0f;
    in.engine_rpm        = 0U;
    TEST_ASSERT_FALSE(reb_rules_safe_to_block(&in));
}

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c false when
 *        engine RPM exceeds @c REB_ENGINE_RPM_LIMIT.
 * @details MC/DC: C2 (rpm <= limit) = false makes the expression false.
 * @test Group 9 — blocking safety rules / MC/DC
 * @see reb_rules_safe_to_block
 */
static void test_rules_safe_rpm_too_high(void)
{
    /* MC/DC: C2 (rpm<=limit) = false -> false */
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = 0.0f;
    in.engine_rpm        = REB_ENGINE_RPM_LIMIT + 1U;
    TEST_ASSERT_FALSE(reb_rules_safe_to_block(&in));
}

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c true when both
 *        speed and RPM are exactly at their boundary values.
 * @test Group 9 — blocking safety rules / boundary
 * @see reb_rules_safe_to_block
 */
static void test_rules_safe_both_at_boundary(void)
{
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = REB_MAX_SPEED_FOR_BLOCK_KMH;
    in.engine_rpm        = REB_ENGINE_RPM_LIMIT;
    TEST_ASSERT_TRUE(reb_rules_safe_to_block(&in));
}

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c true when both
 *        speed and RPM are zero (fully stopped, engine off).
 * @test Group 9 — blocking safety rules
 * @see reb_rules_safe_to_block
 */
static void test_rules_safe_speed_zero_rpm_zero(void)
{
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = 0.0f;
    in.engine_rpm        = 0U;
    TEST_ASSERT_TRUE(reb_rules_safe_to_block(&in));
}

/**
 * @brief Verifies that @c reb_rules_safe_to_block() returns @c false when
 *        both speed and RPM exceed their respective limits.
 * @test Group 9 — blocking safety rules / MC/DC
 * @see reb_rules_safe_to_block
 */
static void test_rules_both_exceed_limits(void)
{
    /* Both conditions false */
    RebInputs in = make_inputs();
    in.vehicle_speed_kmh = REB_MAX_SPEED_FOR_BLOCK_KMH + 10.0f;
    in.engine_rpm        = REB_ENGINE_RPM_LIMIT + 100U;
    TEST_ASSERT_FALSE(reb_rules_safe_to_block(&in));
}

/*==============================================================================
 * Group 10: reb_core_init (persistence branch coverage)
 *==============================================================================*/

/**
 * @brief Verifies that @c reb_core_init() initialises the context to
 *        @c REB_STATE_IDLE via @c reb_state_machine_init() when no valid
 *        persistence file exists (first boot).
 * @details The absence of the persistence file causes @c reb_persistence_load()
 *          to return @c false, and @c reb_core_init() must fall back to a
 *          clean state machine initialisation.
 * @test Group 10 — core initialisation
 * @see reb_core_init, NFR-REL-001 (REB-SRS-001)
 */
static void test_core_init_first_boot_initializes_state_machine(void)
{
    _persist_clear();   /* no file - first boot */
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_core_init(&ctx);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that @c reb_core_init() restores a previously saved context
 *        when a valid persistence file exists.
 * @details A @c REB_STATE_THEFT_CONFIRMED context with a known timestamp is
 *          written to the persistence file. @c reb_core_init() must load it
 *          without resetting to IDLE. Corresponds to NFR-REL-001: state
 *          recovery after power loss.
 * @test Group 10 — core initialisation
 * @see reb_core_init, NFR-REL-001 (REB-SRS-001)
 */
static void test_core_init_with_saved_state_loads_context(void)
{
    RebContext saved;
    memset(&saved, 0, sizeof(saved));
    saved.current_state = REB_STATE_THEFT_CONFIRMED;
    saved.theft_confirmed_timestamp_ms = 9999U;
    _persist_write(&saved);

    RebContext ctx;
    reb_core_init(&ctx);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(9999U, ctx.theft_confirmed_timestamp_ms);
    _persist_clear();
}

/*==============================================================================
 * Group 11: reb_core_execute
 *==============================================================================*/

/**
 * @brief Verifies that @c reb_core_execute() delegates to
 *        @c reb_state_machine_step() by confirming a state transition when an
 *        intrusion event is injected.
 * @test Group 11 — core execution
 * @see reb_core_execute
 */
static void test_core_execute_calls_state_machine(void)
{
    _persist_clear();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;

    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that @c reb_core_execute() saves the updated context to the
 *        persistence file after each execution cycle.
 * @details After a call with no active event, the persistence file must contain
 *          a valid context with @c current_state = @c REB_STATE_IDLE. Corresponds
 *          to NFR-REL-001.
 * @test Group 11 — core execution
 * @see reb_core_execute, NFR-REL-001 (REB-SRS-001)
 */
static void test_core_execute_saves_state(void)
{
    _persist_clear();
    _persist_prepare_dir();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    reb_core_execute(&ctx, &in, &out);

    /* Verify that the file was written and contains IDLE */
    RebContext loaded;
    TEST_ASSERT_TRUE(_persist_read(&loaded));
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, loaded.current_state);
}

/**
 * @brief Verifies that @c reb_core_execute() drives the full
 *        IDLE → THEFT_CONFIRMED → BLOCKING → BLOCKED cycle when the appropriate
 *        inputs are supplied in sequence.
 * @details Models the normal-operation scenario from SRS §7.1.1. Each call
 *          corresponds to one control-loop iteration.
 * @test Group 11 — core execution / integration
 * @see reb_core_execute, FR-001, FR-010, FR-011 (REB-SRS-001)
 */
static void test_core_execute_full_block_cycle(void)
{
    _persist_clear();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;

    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;
    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);

    in.intrusion_detected = false;
    in.timestamp_ms = T_CONFIRMED;
    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);

    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_core_execute(&ctx, &in, &out);

    in.timestamp_ms = T_BLOCKED_TS;
    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/*==============================================================================
 * Group 12: Boundary conditions
 *==============================================================================*/

/**
 * @brief Verifies that an intrusion event alone (with @c REB_REMOTE_NONE)
 *        is sufficient to transition from IDLE to THEFT_CONFIRMED.
 * @details Exercises the OR condition in the IDLE handler: intrusion OR valid
 *          remote BLOCK. Only intrusion is true here.
 * @test Group 12 — boundary conditions
 * @see FR-001, FR-002 (REB-SRS-001)
 */
static void test_idle_intrusion_only_no_block_cmd(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = true;
    in.remote_command     = REB_REMOTE_NONE;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that a valid @c REB_REMOTE_BLOCK command alone (without
 *        intrusion) transitions from IDLE to THEFT_CONFIRMED.
 * @details Exercises the second branch of the OR condition in the IDLE handler.
 * @test Group 12 — boundary conditions
 * @see FR-001, FR-002, FR-005 (REB-SRS-001)
 */
static void test_idle_block_cmd_valid_nonce_no_intrusion(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = false;
    in.remote_command     = REB_REMOTE_BLOCK;
    in.nonce              = 1U;
    in.timestamp_ms       = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that a @c REB_REMOTE_BLOCK command with an invalid nonce
 *        (no intrusion present) leaves the state in IDLE.
 * @details Neither branch of the IDLE condition is true: no intrusion, and the
 *          BLOCK command fails security validation.
 * @test Group 12 — boundary conditions
 * @see FR-002, NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_block_cmd_invalid_nonce_stays_idle(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 100U;
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = false;
    in.remote_command     = REB_REMOTE_BLOCK;
    in.nonce              = 5U;
    in.timestamp_ms       = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that an @c REB_REMOTE_UNLOCK command with an invalid nonce
 *        (nonce = 0, which is not in window) does not release the BLOCKED state.
 * @test Group 12 — boundary conditions
 * @see SRS §6.2 (REB-SRS-001)
 */
static void test_blocked_unlock_cmd_not_allowed_stays_blocked(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command    = REB_REMOTE_UNLOCK;
    in.nonce             = 0U;
    in.timestamp_ms      = T_BLOCKED_TS + 1U;
    in.tcu_ack_received  = true;

    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/**
 * @brief Verifies that the BLOCKED state remains stable when no remote command
 *        is present (@c REB_REMOTE_NONE).
 * @test Group 12 — boundary conditions
 * @see FR-011, SRS §6.1 (REB-SRS-001)
 */
static void test_blocked_no_cmd_stays_blocked(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_NONE;
    in.timestamp_ms   = T_BLOCKED_TS + 1U;

    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
}

/**
 * @brief Verifies that @c derate_percent never exceeds @c REB_DERATE_MAX_PERCENT
 *        regardless of how many consecutive steps are executed at speed above
 *        @c REB_SAFE_MOVING_SPEED_KMH.
 * @details Each step increments derating by one step. After many iterations the
 *          value must be clamped at @c REB_DERATE_MAX_PERCENT. Corresponds to
 *          FR-009.
 * @test Group 12 — boundary conditions / derating clamp
 * @see FR-009 (REB-SRS-001)
 */
static void test_derating_clamped_at_max(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = REB_SAFE_MOVING_SPEED_KMH + 1.0f;
    in.timestamp_ms      = T_CONFIRMED + 50U;

    for (int i = 0; i < 20; i++)
    {
        in.timestamp_ms += 100U;
        reb_state_machine_step(&ctx, &in, &out);
        if (ctx.current_state != REB_STATE_BLOCKING) break;
        TEST_ASSERT_LESS_OR_EQUAL(REB_DERATE_MAX_PERCENT, out.derate_percent);
    }
}

/**
 * @brief Verifies that @c reb_core_execute() completes without crashing when
 *        no prior persistence file exists and the save fails silently.
 * @details Models a first-boot scenario where the output directory may not yet
 *          exist. The system must remain in @c REB_STATE_IDLE and not abort.
 * @test Group 12 — boundary conditions / robustness
 * @see reb_core_execute (REB-SRS-001)
 */
static void test_core_execute_save_fails_gracefully(void)
{
    /* Even without a prior file, execute must not crash */
    _persist_clear();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that two consecutive valid BLOCK commands, each with a
 *        fresh nonce, both succeed in triggering the IDLE → THEFT_CONFIRMED
 *        transition when the context is manually reset between them.
 * @test Group 12 — boundary conditions
 * @see FR-001, FR-002, NFR-SEC-001 (REB-SRS-001)
 */
static void test_idle_two_consecutive_valid_blocks(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = 1000U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);

    ctx.current_state = REB_STATE_IDLE;
    in.nonce = ctx.last_valid_nonce + 1U;
    in.timestamp_ms = 2000U;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/*==============================================================================
 * Group 13: reb_config.h — constant validation
 *==============================================================================*/

/**
 * @brief Verifies that @c REB_THEFT_CONFIRM_WINDOW_MS is exactly 60 000 ms
 *        (60 seconds), as defined in SRS §6.1.
 * @test Group 13 — configuration constants
 * @see FR-008, SRS §6.1 (REB-SRS-001)
 */
static void test_config_theft_confirm_window_ms(void)
{
    /* SRS §6.1: safety window = 60 s */
    TEST_ASSERT_EQUAL_UINT32(60000U, REB_THEFT_CONFIRM_WINDOW_MS);
}

/**
 * @brief Verifies that @c REB_STOP_HOLD_TIME_MS is exactly 120 000 ms
 *        (120 seconds), as required by FR-010 and FR-011.
 * @test Group 13 — configuration constants
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_config_stop_hold_time_ms(void)
{
    /* FR-010/FR-011: 120 s safe-stop dwell */
    TEST_ASSERT_EQUAL_UINT32(120000U, REB_STOP_HOLD_TIME_MS);
}

/**
 * @brief Verifies that @c REB_BLOCKED_RETRANSMIT_MS is exactly 5 000 ms
 *        (5 seconds), as described in SRS §6.1 BLOCKED state.
 * @test Group 13 — configuration constants
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_config_blocked_retransmit_ms(void)
{
    /* SRS §6.1 BLOCKED: retransmit every 5 s */
    TEST_ASSERT_EQUAL_UINT32(5000U, REB_BLOCKED_RETRANSMIT_MS);
}

/**
 * @brief Verifies that @c REB_MAX_ALLOWED_SPEED_FOR_LOCK is 0.5 m/s, the
 *        maximum speed at which the starter lock may be engaged.
 * @test Group 13 — configuration constants
 * @see NFR-SAF-001 (REB-SRS-001)
 */
static void test_config_max_allowed_speed_for_lock(void)
{
    /* NFR-SAF-001: no lock while moving */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, REB_MAX_ALLOWED_SPEED_FOR_LOCK);
}

/**
 * @brief Verifies that @c REB_SAFE_MOVING_SPEED_KMH is 5.0 km/h.
 * @test Group 13 — configuration constants
 */
static void test_config_safe_moving_speed_kmh(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, REB_SAFE_MOVING_SPEED_KMH);
}

/**
 * @brief Verifies that @c REB_MAX_SPEED_FOR_BLOCK_KMH is 5.0 km/h.
 * @test Group 13 — configuration constants
 */
static void test_config_max_speed_for_block_kmh(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, REB_MAX_SPEED_FOR_BLOCK_KMH);
}

/**
 * @brief Verifies that @c REB_DERATE_STEP_PERCENT is 10 %.
 * @test Group 13 — configuration constants
 * @see FR-009 (REB-SRS-001)
 */
static void test_config_derate_step_percent(void)
{
    TEST_ASSERT_EQUAL_UINT32(10U, REB_DERATE_STEP_PERCENT);
}

/**
 * @brief Verifies that @c REB_DERATE_MAX_PERCENT is 90 %, as specified in
 *        FR-009.
 * @test Group 13 — configuration constants
 * @see FR-009 (REB-SRS-001)
 */
static void test_config_derate_max_percent(void)
{
    /* FR-009: maximum derating = 90% */
    TEST_ASSERT_EQUAL_UINT32(90U, REB_DERATE_MAX_PERCENT);
}

/**
 * @brief Verifies that @c REB_DERATE_MIN_PERCENT is 20 % and is at least 10 %,
 *        satisfying the minimum fuel safety floor required by FR-009.
 * @test Group 13 — configuration constants
 * @see FR-009 (REB-SRS-001)
 */
static void test_config_derate_min_percent(void)
{
    /* FR-009: minimum fuel floor >= 10% */
    TEST_ASSERT_EQUAL_UINT32(20U, REB_DERATE_MIN_PERCENT);
    TEST_ASSERT_GREATER_OR_EQUAL(10U, REB_DERATE_MIN_PERCENT); /* satisfies FR-009 */
}

/**
 * @brief Verifies that @c REB_MAX_INVALID_ATTEMPTS is 3, consistent with the
 *        lockout-after-three-failures requirement in SRS §6.2.
 * @test Group 13 — configuration constants
 * @see SRS §6.2 (REB-SRS-001)
 */
static void test_config_max_invalid_attempts(void)
{
    /* SRS §6.2 Recovery Procedure: lockout after 3 failed attempts */
    TEST_ASSERT_EQUAL_UINT32(3U, REB_MAX_INVALID_ATTEMPTS);
}

/**
 * @brief Verifies that @c REB_NONCE_WINDOW_SIZE is 32, as required by
 *        NFR-SEC-001.
 * @test Group 13 — configuration constants
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_config_nonce_window_size(void)
{
    /* NFR-SEC-001 */
    TEST_ASSERT_EQUAL_UINT32(32U, REB_NONCE_WINDOW_SIZE);
}

/**
 * @brief Verifies that @c REB_NONCE_HISTORY_SIZE is 10.
 * @test Group 13 — configuration constants
 */
static void test_config_nonce_history_size(void)
{
    TEST_ASSERT_EQUAL_UINT32(10U, REB_NONCE_HISTORY_SIZE);
}

/**
 * @brief Verifies that @c REB_MIN_BATTERY_VOLTAGE is 9.0 V.
 * @test Group 13 — configuration constants
 */
static void test_config_min_battery_voltage(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.0f, REB_MIN_BATTERY_VOLTAGE);
}

/**
 * @brief Verifies that @c REB_ENGINE_RPM_LIMIT is 1 000 RPM.
 * @test Group 13 — configuration constants
 */
static void test_config_engine_rpm_limit(void)
{
    TEST_ASSERT_EQUAL_UINT32(1000U, REB_ENGINE_RPM_LIMIT);
}

/**
 * @brief Verifies the relationship @c REB_DERATE_MAX_PERCENT >
 *        @c REB_DERATE_MIN_PERCENT.
 * @details A maximum derating value that is not strictly greater than the
 *          minimum would make the derating ramp degenerate.
 * @test Group 13 — configuration constants / sanity
 */
static void test_config_derate_max_greater_than_min(void)
{
    /* Sanity: max derating > min derating */
    TEST_ASSERT_GREATER_THAN(REB_DERATE_MIN_PERCENT, REB_DERATE_MAX_PERCENT);
}

/**
 * @brief Verifies that at least one derating step fits between @c
 *        REB_DERATE_MIN_PERCENT and @c REB_DERATE_MAX_PERCENT.
 * @details If the range is smaller than one step, the incremental ramp would
 *          never make progress, breaking FR-009.
 * @test Group 13 — configuration constants / sanity
 * @see FR-009 (REB-SRS-001)
 */
static void test_config_derate_step_fits_in_max(void)
{
    /* At least one step must fit between min and max */
    TEST_ASSERT_GREATER_THAN(0U, (REB_DERATE_MAX_PERCENT - REB_DERATE_MIN_PERCENT)
                                  / REB_DERATE_STEP_PERCENT);
}

/**
 * @brief Verifies that @c REB_NONCE_WINDOW_SIZE >= @c REB_NONCE_HISTORY_SIZE.
 * @details The sliding window must be at least as large as the replay-detection
 *          history to ensure that all recently accepted nonces remain within the
 *          window and can be checked for replay.
 * @test Group 13 — configuration constants / sanity
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_config_nonce_window_geq_history(void)
{
    /* Window should be >= history size for proper anti-replay coverage */
    TEST_ASSERT_GREATER_OR_EQUAL(REB_NONCE_HISTORY_SIZE, REB_NONCE_WINDOW_SIZE);
}

/*==============================================================================
 * Group 14: reb_types.h — enumerations, struct sizes, and field accessibility
 *==============================================================================*/

/**
 * @brief Verifies that @c REB_STATE_IDLE has the integer value 0.
 * @details Zero-initialisation of @c RebContext must produce a context in the
 *          IDLE state without any explicit assignment.
 * @test Group 14 — type definitions
 */
static void test_types_reb_state_idle_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)REB_STATE_IDLE);
}

/**
 * @brief Verifies the ordinal sequence of all @c RebState enumeration values.
 * @details Confirms IDLE = 0, THEFT_CONFIRMED = 1, BLOCKING = 2, BLOCKED = 3,
 *          as defined in @c reb_types.h.
 * @test Group 14 — type definitions
 */
static void test_types_reb_state_sequence(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)REB_STATE_IDLE);
    TEST_ASSERT_EQUAL_INT(1, (int)REB_STATE_THEFT_CONFIRMED);
    TEST_ASSERT_EQUAL_INT(2, (int)REB_STATE_BLOCKING);
    TEST_ASSERT_EQUAL_INT(3, (int)REB_STATE_BLOCKED);
}

/**
 * @brief Verifies that @c REB_REMOTE_NONE has the integer value 0.
 * @test Group 14 — type definitions
 */
static void test_types_remote_command_none_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)REB_REMOTE_NONE);
}

/**
 * @brief Verifies the ordinal sequence of all @c RebRemoteCommand enumeration
 *        values: NONE = 0, BLOCK = 1, UNLOCK = 2, CANCEL = 3.
 * @test Group 14 — type definitions
 */
static void test_types_remote_command_sequence(void)
{
    TEST_ASSERT_EQUAL_INT(0, (int)REB_REMOTE_NONE);
    TEST_ASSERT_EQUAL_INT(1, (int)REB_REMOTE_BLOCK);
    TEST_ASSERT_EQUAL_INT(2, (int)REB_REMOTE_UNLOCK);
    TEST_ASSERT_EQUAL_INT(3, (int)REB_REMOTE_CANCEL);
}

/**
 * @brief Verifies that @c sizeof(RebContext) is non-zero.
 * @test Group 14 — type definitions
 */
static void test_types_context_size_nonzero(void)
{
    TEST_ASSERT_GREATER_THAN(0U, sizeof(RebContext));
}

/**
 * @brief Verifies that @c sizeof(RebInputs) is non-zero.
 * @test Group 14 — type definitions
 */
static void test_types_inputs_size_nonzero(void)
{
    TEST_ASSERT_GREATER_THAN(0U, sizeof(RebInputs));
}

/**
 * @brief Verifies that @c sizeof(RebOutputs) is non-zero.
 * @test Group 14 — type definitions
 */
static void test_types_outputs_size_nonzero(void)
{
    TEST_ASSERT_GREATER_THAN(0U, sizeof(RebOutputs));
}

/**
 * @brief Verifies that the @c nonce_history array in @c RebContext contains
 *        exactly @c REB_NONCE_HISTORY_SIZE elements.
 * @details Confirms that the array declaration in @c reb_types.h matches the
 *          configured history size.
 * @test Group 14 — type definitions
 */
static void test_types_context_nonce_history_field_size(void)
{
    RebContext ctx;
    /* nonce_history must hold REB_NONCE_HISTORY_SIZE entries */
    TEST_ASSERT_EQUAL(REB_NONCE_HISTORY_SIZE,
                      sizeof(ctx.nonce_history) / sizeof(ctx.nonce_history[0]));
}

/**
 * @brief Verifies that all declared fields of @c RebInputs are accessible and
 *        hold assigned values correctly.
 * @details Checks that the struct layout is correct by writing to every field
 *          and reading back selected values.
 * @test Group 14 — type definitions
 */
static void test_types_inputs_all_fields_accessible(void)
{
    RebInputs in;
    memset(&in, 0, sizeof(in));
    in.intrusion_detected = true;
    in.ignition_on = true;
    in.engine_running = true;
    in.tcu_ack_received = true;
    in.battery_voltage = 12.5f;
    in.engine_rpm = 800U;
    in.vehicle_speed_kmh = 30.0f;
    in.remote_command = REB_REMOTE_BLOCK;
    in.timestamp_ms = 1000U;
    in.nonce = 42U;
    TEST_ASSERT_TRUE(in.intrusion_detected);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, in.battery_voltage);
    TEST_ASSERT_EQUAL_UINT16(800U, in.engine_rpm);
}

/**
 * @brief Verifies that all declared fields of @c RebOutputs are accessible and
 *        hold assigned values correctly.
 * @test Group 14 — type definitions
 */
static void test_types_outputs_all_fields_accessible(void)
{
    RebOutputs out;
    memset(&out, 0, sizeof(out));
    out.visual_alert = true;
    out.acoustic_alert = true;
    out.starter_lock = true;
    out.derate_percent = 50U;
    out.send_status_to_tcu = true;
    TEST_ASSERT_TRUE(out.visual_alert);
    TEST_ASSERT_EQUAL_UINT8(50U, out.derate_percent);
}

/**
 * @brief Verifies that a zero-initialised @c RebContext has
 *        @c current_state == @c REB_STATE_IDLE.
 * @details Since @c REB_STATE_IDLE = 0, a @c memset-to-zero must produce a
 *          context in the IDLE state, which is the correct default.
 * @test Group 14 — type definitions
 */
static void test_types_context_initial_state_field(void)
{
    RebContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    /* After memset to zero, current_state should be REB_STATE_IDLE (= 0) */
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/*==============================================================================
 * Group 15: reb_logger — interface robustness
 *==============================================================================*/

/**
 * @brief Verifies that @c reb_logger_info() does not crash when called with a
 *        typical informational message string.
 * @test Group 15 — logger interface
 * @see reb_logger_info
 */
static void test_logger_info_does_not_crash(void)
{
    /* reb_logger_info must be callable without crashing */
    reb_logger_info("System initialized");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_logger_warn() does not crash when called with a
 *        typical warning message string.
 * @test Group 15 — logger interface
 * @see reb_logger_warn
 */
static void test_logger_warn_does_not_crash(void)
{
    reb_logger_warn("Authentication attempt failed");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_logger_error() does not crash when called with a
 *        typical error message string.
 * @test Group 15 — logger interface
 * @see reb_logger_error
 */
static void test_logger_error_does_not_crash(void)
{
    reb_logger_error("MAX_INVALID_ATTEMPTS exceeded");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_logger_info() handles an empty string without
 *        crashing.
 * @test Group 15 — logger interface / edge case
 * @see reb_logger_info
 */
static void test_logger_info_empty_string(void)
{
    reb_logger_info("");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_logger_warn() handles an empty string without
 *        crashing.
 * @test Group 15 — logger interface / edge case
 * @see reb_logger_warn
 */
static void test_logger_warn_empty_string(void)
{
    reb_logger_warn("");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_logger_error() handles an empty string without
 *        crashing.
 * @test Group 15 — logger interface / edge case
 * @see reb_logger_error
 */
static void test_logger_error_empty_string(void)
{
    reb_logger_error("");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_logger_info() handles multiple consecutive calls
 *        without crashing or corrupting state.
 * @test Group 15 — logger interface
 * @see reb_logger_info
 */
static void test_logger_multiple_calls_same_level(void)
{
    reb_logger_info("Event 1");
    reb_logger_info("Event 2");
    reb_logger_info("Event 3");
    TEST_PASS();
}

/**
 * @brief Verifies that all three log levels can be called in an interleaved
 *        sequence without crashing.
 * @test Group 15 — logger interface
 * @see reb_logger_info, reb_logger_warn, reb_logger_error
 */
static void test_logger_interleaved_levels(void)
{
    reb_logger_info("System started");
    reb_logger_warn("Voltage low");
    reb_logger_error("Lock failure");
    reb_logger_info("Recovered");
    TEST_PASS();
}

/*==============================================================================
 * Group 16: reb_persistence (NFR-REL-001)
 *==============================================================================*/

/**
 * @brief Verifies that a persisted @c REB_STATE_BLOCKED context, including
 *        @c last_blocked_retx_timestamp_ms, is fully restored by
 *        @c reb_core_init() after a simulated power cycle.
 * @details Corresponds to NFR-REL-001: the system must resume the BLOCKED state
 *          after any power interruption without user interaction.
 * @test Group 16 — persistence
 * @see NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_load_blocked_state_restores(void)
{
    RebContext saved;
    memset(&saved, 0, sizeof(saved));
    saved.current_state = REB_STATE_BLOCKED;
    saved.last_blocked_retx_timestamp_ms = 5000U;
    _persist_write(&saved);

    RebContext ctx;
    reb_core_init(&ctx);

    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(5000U, ctx.last_blocked_retx_timestamp_ms);
    _persist_clear();
}

/**
 * @brief Verifies that a persisted @c REB_STATE_BLOCKING context, including
 *        @c vehicle_stopped_timestamp_ms, is fully restored by
 *        @c reb_core_init().
 * @details A restart during the BLOCKING state must resume from the same
 *          position in the dwell sequence, not from zero.
 * @test Group 16 — persistence
 * @see NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_load_blocking_state_restores(void)
{
    RebContext saved;
    memset(&saved, 0, sizeof(saved));
    saved.current_state = REB_STATE_BLOCKING;
    saved.vehicle_stopped_timestamp_ms = 12345U;
    _persist_write(&saved);

    RebContext ctx;
    reb_core_init(&ctx);

    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKING, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(12345U, ctx.vehicle_stopped_timestamp_ms);
    _persist_clear();
}

/**
 * @brief Verifies that @c invalid_unlock_attempts is preserved across a
 *        simulated power cycle.
 * @details The brute-force counter must survive a power cycle to prevent
 *          lockout bypass by battery disconnection, as required by NFR-REL-001
 *          and the recovery procedure in SRS §6.2.
 * @test Group 16 — persistence
 * @see NFR-REL-001, SRS §6.2 (REB-SRS-001)
 */
static void test_persistence_load_restores_invalid_attempts(void)
{
    RebContext saved;
    memset(&saved, 0, sizeof(saved));
    saved.current_state = REB_STATE_BLOCKED;
    saved.invalid_unlock_attempts = 2U;
    _persist_write(&saved);

    RebContext ctx;
    reb_core_init(&ctx);

    TEST_ASSERT_EQUAL_UINT8(2U, ctx.invalid_unlock_attempts);
    _persist_clear();
}

/**
 * @brief Verifies that @c last_valid_nonce and @c nonce_history[] are preserved
 *        across a simulated power cycle.
 * @details The nonce state must survive a power cycle so that previously used
 *          nonces remain detectable as replays after restart, supporting
 *          NFR-SEC-001 and NFR-REL-001.
 * @test Group 16 — persistence
 * @see NFR-REL-001, NFR-SEC-001 (REB-SRS-001)
 */
static void test_persistence_load_restores_nonce_history(void)
{
    RebContext saved;
    memset(&saved, 0, sizeof(saved));
    saved.current_state = REB_STATE_BLOCKED;
    saved.last_valid_nonce = 77U;
    saved.nonce_history[0] = 70U;
    saved.nonce_history[1] = 72U;
    _persist_write(&saved);

    RebContext ctx;
    reb_core_init(&ctx);

    TEST_ASSERT_EQUAL_UINT32(77U, ctx.last_valid_nonce);
    TEST_ASSERT_EQUAL_UINT32(70U, ctx.nonce_history[0]);
    TEST_ASSERT_EQUAL_UINT32(72U, ctx.nonce_history[1]);
    _persist_clear();
}

/**
 * @brief Verifies that when @c reb_persistence_load() returns @c false (no
 *        valid file), @c reb_core_init() falls back to a clean state machine
 *        initialisation with all fields at their safe defaults.
 * @details Models first-boot or file-absent scenarios. Corresponds to
 *          NFR-REL-001.
 * @test Group 16 — persistence
 * @see NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_failed_load_triggers_fresh_init(void)
{
    _persist_clear();   /* no file - load fails - clean init */
    RebContext ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    reb_core_init(&ctx);

    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.last_valid_nonce);
    TEST_ASSERT_EQUAL_UINT8(0U, ctx.invalid_unlock_attempts);
}

/**
 * @brief Verifies that @c reb_persistence_load() returns @c false when the
 *        persistence file has a corrupted CRC.
 * @details The last byte of a valid persistence file is overwritten with 0xAA
 *          to simulate data corruption. The CRC check in @c reb_persistence.c
 *          must detect the mismatch and reject the file.
 * @test Group 16 — persistence / error branch
 * @see reb_persistence_load, NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_load_corrupted_crc_returns_false(void)
{
    /* Covers the "return false" branch of the CRC check in reb_persistence.c */
    _persist_clear();
    _persist_prepare_dir();
    RebContext ctx;
    ctx_init(&ctx);
    reb_persistence_save(&ctx);

    /* Corrupt the last byte of the file */
    FILE *f = fopen(BIN_PATH, "r+b");
    if (f)
    {
        fseek(f, -1L, SEEK_END);
        uint8_t bad = 0xAA;
        fwrite(&bad, 1, 1, f);
        fclose(f);
    }
    RebContext loaded;
    TEST_ASSERT_FALSE(reb_persistence_load(&loaded));
    _persist_clear();
}

/**
 * @brief Verifies that @c reb_logger_info() handles the case where @c fopen()
 *        fails gracefully, without crashing.
 * @details A regular file named @c artifacts is created to block the logger's
 *          @c mkdir() call, causing @c fopen() to fail. The logger must not
 *          abort in this condition.
 * @test Group 16 — logger / robustness
 * @see reb_logger_info
 */
static void test_logger_fopen_fails_no_crash(void)
{
    /* Force fopen to fail: create a regular file where the logger expects
       the artifacts/ directory, preventing mkdir from succeeding */
    remove("artifacts/logs/reb.log");
    rmdir("artifacts/logs");
    rmdir("artifacts");

    /* Create "artifacts" as a regular file (not a directory) */
    FILE *blocker = fopen("artifacts", "w");
    if (blocker) fclose(blocker);

    reb_logger_info("should_fail_gracefully");  /* mkdir fails, fopen fails */

    /* Clean up */
    remove("artifacts");
    TEST_PASS();
}

/**
 * @brief Verifies that @c reb_core_execute() saves the updated context after
 *        processing an intrusion event, and that the saved context reflects
 *        the @c REB_STATE_THEFT_CONFIRMED state.
 * @details Confirms that the persistence write occurs on every execution cycle,
 *          as required by NFR-REL-001.
 * @test Group 16 — persistence
 * @see NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_save_called_on_execute(void)
{
    _persist_clear();
    _persist_prepare_dir();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;
    RebOutputs out;
    reb_core_execute(&ctx, &in, &out);

    /* Verify that the file was saved with THEFT_CONFIRMED */
    RebContext loaded;
    TEST_ASSERT_TRUE(_persist_read(&loaded));
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, loaded.current_state);
}

/**
 * @brief Verifies that a complete IDLE → BLOCKED cycle driven through
 *        @c reb_core_execute() results in a saved context in
 *        @c REB_STATE_BLOCKED, confirming end-to-end persistence of the
 *        immobilised state.
 * @details Corresponds to the NFR-REL-001 acceptance criterion: if power is
 *          cut while BLOCKED, the system must re-enter BLOCKED after restart.
 * @test Group 16 — persistence / integration
 * @see NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_blocked_state_saved_and_resumed(void)
{
    _persist_clear();
    _persist_prepare_dir();
    RebContext ctx;
    reb_core_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;

    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;
    reb_core_execute(&ctx, &in, &out);

    in.intrusion_detected = false;
    in.timestamp_ms = T_CONFIRMED;
    reb_core_execute(&ctx, &in, &out);

    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_core_execute(&ctx, &in, &out);

    in.timestamp_ms = T_BLOCKED_TS;
    reb_core_execute(&ctx, &in, &out);

    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);

    /* Verify that the saved file also contains BLOCKED */
    RebContext loaded;
    TEST_ASSERT_TRUE(_persist_read(&loaded));
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, loaded.current_state);
}

/**
 * @brief Verifies that @c reb_persistence_load() sets the
 *        @c persisted_state_valid flag to @c true on a successful load.
 * @details This flag allows the rest of the system to distinguish between a
 *          fresh initialisation and a restored state.
 * @test Group 16 — persistence
 * @see reb_persistence_load, NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_load_sets_persisted_state_valid_flag(void)
{
    RebContext saved;
    memset(&saved, 0, sizeof(saved));
    saved.current_state = REB_STATE_IDLE;
    _persist_write(&saved);

    RebContext ctx;
    reb_core_init(&ctx);

    /* persisted_state_valid is set by reb_persistence_load on success */
    TEST_ASSERT_TRUE(ctx.persisted_state_valid);
    _persist_clear();
}

/**
 * @brief Verifies that successive calls to @c reb_core_execute() with different
 *        states produce a persistence file that reflects the most recent state
 *        (last-write wins).
 * @test Group 16 — persistence
 * @see NFR-REL-001 (REB-SRS-001)
 */
static void test_persistence_multiple_saves_last_wins(void)
{
    _persist_clear();
    _persist_prepare_dir();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;

    reb_core_execute(&ctx, &in, &out);
    RebContext snap1;
    _persist_read(&snap1);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, snap1.current_state);

    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;
    reb_core_execute(&ctx, &in, &out);
    RebContext snap2;
    _persist_read(&snap2);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, snap2.current_state);
}

/*==============================================================================
 * Group 17: reb_security — additional coverage
 *==============================================================================*/

/**
 * @brief Verifies that a @c REB_REMOTE_UNLOCK command with a valid nonce passes
 *        @c reb_security_validate_remote_command().
 * @details Confirms that the validation function accepts UNLOCK commands on the
 *          same terms as BLOCK commands, as required by the unlock path in
 *          SRS §6.2.
 * @test Group 17 — security additional coverage
 * @see NFR-SEC-001, SRS §6.2 (REB-SRS-001)
 */
static void test_security_unlock_with_valid_nonce_passes_validate(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_UNLOCK;
    in.nonce          = 10U;
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that a full nonce history does not prevent fresh, new nonces
 *        from being accepted.
 * @details The history buffer is pre-filled with nonces 1 through
 *          @c REB_NONCE_HISTORY_SIZE, and @c last_valid_nonce is set to
 *          @c REB_NONCE_HISTORY_SIZE. A command with nonce
 *          @c REB_NONCE_HISTORY_SIZE + 1 must be accepted.
 * @test Group 17 — security additional coverage
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_nonce_history_does_not_block_new_nonces(void)
{
    /* Filling history with old nonces should not block new valid nonces */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    /* Fill history */
    for (int i = 0; i < REB_NONCE_HISTORY_SIZE; i++)
        ctx.nonce_history[i] = (uint32_t)(i + 1U);
    ctx.last_valid_nonce = (uint32_t)REB_NONCE_HISTORY_SIZE;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = (uint32_t)(REB_NONCE_HISTORY_SIZE + 1U); /* fresh nonce */
    in.timestamp_ms   = 1000U;

    TEST_ASSERT_TRUE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that, after the history buffer has wrapped around, a nonce
 *        that is no longer in the buffer is still rejected by the sliding-window
 *        check because it is below @c last_valid_nonce.
 * @details After @c REB_NONCE_HISTORY_SIZE successful validations, slot 0 is
 *          overwritten. Nonce 1 is no longer in history, but it is still
 *          rejected because nonce 1 <= last_valid_nonce. This test confirms the
 *          interaction between the sliding-window and history checks.
 * @test Group 17 — security additional coverage
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_replay_after_window_wrap(void)
{
    /* After filling history, oldest nonce is overwritten; replaying it is
       accepted from a history perspective but rejected by the window check. */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.timestamp_ms   = 1000U;

    /* Fill history with nonces 1..HISTORY_SIZE */
    for (uint32_t i = 1U; i <= (uint32_t)REB_NONCE_HISTORY_SIZE; i++)
    {
        in.nonce = ctx.last_valid_nonce + 1U;
        reb_security_validate_remote_command(&in, &ctx);
    }

    /* Nonce 1 is no longer in history, but nonce 1 <= last_valid_nonce
       -> still rejected by window check */
    in.nonce = 1U;
    TEST_ASSERT_FALSE(reb_security_validate_remote_command(&in, &ctx));
}

/**
 * @brief Verifies that each successful validation increments
 *        @c nonce_history_index by 1, modulo @c REB_NONCE_HISTORY_SIZE.
 * @test Group 17 — security additional coverage
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_validate_increments_nonce_index(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command = REB_REMOTE_BLOCK;
    in.nonce          = 1U;
    in.timestamp_ms   = 1000U;

    uint8_t idx_before = ctx.nonce_history_index;
    reb_security_validate_remote_command(&in, &ctx);
    TEST_ASSERT_EQUAL_UINT8((idx_before + 1U) % REB_NONCE_HISTORY_SIZE,
                            ctx.nonce_history_index);
}

/**
 * @brief Verifies that @c reb_security_unlock_allowed() returns @c false when
 *        nonce is 0 and @c last_valid_nonce is 0, causing the sliding-window
 *        check to fail (nonce <= last).
 * @test Group 17 — security additional coverage
 * @see NFR-SEC-001 (REB-SRS-001)
 */
static void test_security_unlock_allowed_with_zero_nonce_rejected(void)
{
    /* nonce = 0, last_valid_nonce = 0 -> nonce <= last -> rejected */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    in.remote_command   = REB_REMOTE_UNLOCK;
    in.nonce            = 0U;
    in.timestamp_ms     = 1000U;
    in.tcu_ack_received = true;

    TEST_ASSERT_FALSE(reb_security_unlock_allowed(&in, &ctx));
}

/*==============================================================================
 * Group 18: reb_state_machine — additional branch coverage
 *==============================================================================*/

/**
 * @brief Exercises the complete IDLE → THEFT_CONFIRMED → BLOCKING → BLOCKED →
 *        IDLE cycle through @c reb_core_execute(), including an authenticated
 *        unlock at the end.
 * @details Models the normal-operation scenario from SRS §7.1.1 and the
 *          recovery procedure from SRS §6.2 in a single test.
 * @test Group 18 — state machine additional coverage / integration
 * @see FR-001, FR-011, SRS §6.2, SRS §7.1.1 (REB-SRS-001)
 */
static void test_sm_full_cycle_with_unlock(void)
{
    _persist_clear();
    _persist_prepare_dir();
    RebContext ctx;
    reb_core_init(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;

    /* Block cycle */
    in.intrusion_detected = true;
    in.timestamp_ms = T_INTRUSION;
    reb_core_execute(&ctx, &in, &out);

    in.intrusion_detected = false;
    in.timestamp_ms = T_CONFIRMED;
    reb_core_execute(&ctx, &in, &out);

    in.vehicle_speed_kmh = 0.0f;
    in.timestamp_ms = T_CONFIRMED + 100U;
    reb_core_execute(&ctx, &in, &out);

    in.timestamp_ms = T_BLOCKED_TS;
    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_BLOCKED, ctx.current_state);

    /* Authenticated unlock */
    prepare_valid_unlock_inputs(&in, &ctx);
    reb_core_execute(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
}

/**
 * @brief Verifies that derating output is always >= @c REB_DERATE_MIN_PERCENT
 *        when the vehicle speed is between @c REB_MAX_ALLOWED_SPEED_FOR_LOCK
 *        and @c REB_SAFE_MOVING_SPEED_KMH.
 * @details Corresponds to the minimum fuel safety floor requirement of FR-009:
 *          at speed 3 km/h the output must be at least 20 %.
 * @test Group 18 — state machine additional coverage
 * @see FR-009 (REB-SRS-001)
 */
static void test_sm_blocking_derating_never_below_min_while_slow(void)
{
    /* FR-009: derating >= 20% (REB_DERATE_MIN_PERCENT) when moving 0.5-5 km/h */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 3.0f; /* between MAX_ALLOWED and SAFE */
    in.timestamp_ms = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);

    TEST_ASSERT_GREATER_OR_EQUAL(REB_DERATE_MIN_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that derating output is always >= @c REB_DERATE_STEP_PERCENT
 *        when the vehicle speed exceeds @c REB_SAFE_MOVING_SPEED_KMH.
 * @details At 100 km/h the incremental derating branch fires and must produce
 *          at least one step of reduction, consistent with FR-009.
 * @test Group 18 — state machine additional coverage
 * @see FR-009 (REB-SRS-001)
 */
static void test_sm_blocking_derating_never_below_step_while_fast(void)
{
    /* FR-009: at least one step of derating when speed > SAFE */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 100.0f;
    in.timestamp_ms = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);

    TEST_ASSERT_GREATER_OR_EQUAL(REB_DERATE_STEP_PERCENT, out.derate_percent);
}

/**
 * @brief Verifies that a successful unlock resets all timing fields to zero
 *        by internally calling @c reb_state_machine_init().
 * @test Group 18 — state machine additional coverage
 * @see reb_state_machine_init, SRS §6.2 (REB-SRS-001)
 */
static void test_sm_blocked_valid_unlock_resets_all_timers(void)
{
    /* After unlock, all timers must be reset (reb_state_machine_init called) */
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    prepare_valid_unlock_inputs(&in, &ctx);
    reb_state_machine_step(&ctx, &in, &out);

    TEST_ASSERT_EQUAL_INT(REB_STATE_IDLE, ctx.current_state);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.theft_confirmed_timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.vehicle_stopped_timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(0U, ctx.invalid_unlock_attempts);
}

/**
 * @brief Verifies that @c vehicle_stopped_timestamp_ms is not set (remains 0)
 *        when the vehicle is moving in the BLOCKING state.
 * @details The stop timestamp must only be recorded when speed is below the
 *          lock threshold, preventing a premature dwell-timer start.
 * @test Group 18 — state machine additional coverage
 * @see FR-010, FR-011 (REB-SRS-001)
 */
static void test_sm_blocking_stopped_ts_not_set_when_moving(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocking(&ctx);

    ctx.vehicle_stopped_timestamp_ms = 0U;
    RebInputs in = make_inputs();
    RebOutputs out;
    in.vehicle_speed_kmh = 10.0f; /* moving */
    in.timestamp_ms = T_CONFIRMED + 50U;
    reb_state_machine_step(&ctx, &in, &out);

    TEST_ASSERT_EQUAL_UINT32(0U, ctx.vehicle_stopped_timestamp_ms);
}

/**
 * @brief Verifies that the IDLE → THEFT_CONFIRMED transition records the
 *        exact input timestamp in @c theft_confirmed_timestamp_ms.
 * @test Group 18 — state machine additional coverage
 * @see FR-001, FR-008 (REB-SRS-001)
 */
static void test_sm_theft_confirmed_stores_timestamp(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = true;
    in.timestamp_ms = 55555U;
    reb_state_machine_step(&ctx, &in, &out);

    TEST_ASSERT_EQUAL_UINT32(55555U, ctx.theft_confirmed_timestamp_ms);
}

/**
 * @brief Verifies that the IDLE → THEFT_CONFIRMED transition fires when both
 *        @c intrusion_detected and a valid @c REB_REMOTE_BLOCK command are
 *        simultaneously present.
 * @details MC/DC: both conditions of the OR expression in the IDLE handler are
 *          true at the same time. The result must be a transition to
 *          THEFT_CONFIRMED regardless of which branch fires first.
 * @test Group 18 — state machine additional coverage / MC/DC
 * @see FR-001, FR-002 (REB-SRS-001)
 */
static void test_sm_idle_intrusion_and_valid_block_both_trigger(void)
{
    /* MC/DC: both conditions true simultaneously - still goes to THEFT_CONFIRMED */
    RebContext ctx;
    ctx_init(&ctx);
    ctx.last_valid_nonce = 0U;

    RebInputs in = make_inputs();
    RebOutputs out;
    in.intrusion_detected = true;
    in.remote_command     = REB_REMOTE_BLOCK;
    in.nonce              = 1U;
    in.timestamp_ms       = T_INTRUSION;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_EQUAL_INT(REB_STATE_THEFT_CONFIRMED, ctx.current_state);
}

/**
 * @brief Verifies that the retransmission fires on the first step after the
 *        interval elapses and is suppressed on the immediately following step
 *        (same timestamp).
 * @details Two consecutive steps are issued with the same timestamp after the
 *          interval. The second step must not retransmit because the timestamp
 *          has not advanced.
 * @test Group 18 — state machine additional coverage
 * @see SRS §6.1 (REB-SRS-001)
 */
static void test_sm_blocked_retransmit_then_no_retransmit(void)
{
    RebContext ctx;
    ctx_init(&ctx);
    drive_to_blocked(&ctx);

    RebInputs in = make_inputs();
    RebOutputs out;
    ctx.last_blocked_retx_timestamp_ms = 0U;

    /* First call: triggers retransmit */
    in.timestamp_ms = REB_BLOCKED_RETRANSMIT_MS;
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_TRUE(out.send_status_to_tcu);

    /* Second call immediately: no retransmit (interval not reached) */
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.send_status_to_tcu);
}

/**
 * @brief Verifies that the output structure is zeroed at the start of each call
 *        to @c reb_state_machine_step(), even if it was pre-filled with 0xFF.
 * @details Stale output values from a previous iteration must not bleed into the
 *          current cycle's output.
 * @test Group 18 — state machine additional coverage
 * @see reb_state_machine_step
 */
static void test_sm_outputs_cleared_each_step(void)
{
    /* outputs are memset to 0 at start of each step */
    RebContext ctx;
    ctx_init(&ctx);
    RebInputs in = make_inputs();
    RebOutputs out;

    memset(&out, 0xFF, sizeof(out));
    reb_state_machine_step(&ctx, &in, &out);
    TEST_ASSERT_FALSE(out.visual_alert);
    TEST_ASSERT_FALSE(out.acoustic_alert);
    TEST_ASSERT_FALSE(out.starter_lock);
}

/*==============================================================================
 * Main entry point
 *==============================================================================*/

/** @brief Unity setUp callback — no per-test setup required. */
void setUp(void)   {}

/** @brief Unity tearDown callback — no per-test teardown required. */
void tearDown(void) {}

/**
 * @brief Test suite entry point.
 * @details Registers and executes all test functions in group order (1–18).
 *          Returns the Unity test result code.
 * @return @c UNITY_END() result (0 = all tests passed, non-zero = failures).
 */
int main(void)
{
    UNITY_BEGIN();

    /* Group 1: reb_state_machine_init */
    RUN_TEST(test_init_sets_idle);
    RUN_TEST(test_init_clears_nonce_history);
    RUN_TEST(test_init_clears_invalid_unlock_attempts);
    RUN_TEST(test_init_clears_timers);
    RUN_TEST(test_init_clears_nonce_history_index);

    /* Group 2: IDLE state */
    RUN_TEST(test_idle_no_event_stays_idle);
    RUN_TEST(test_idle_intrusion_goes_to_theft_confirmed);
    RUN_TEST(test_idle_remote_block_valid_nonce_goes_theft_confirmed);
    RUN_TEST(test_idle_remote_block_invalid_nonce_stays_idle);
    RUN_TEST(test_idle_remote_block_timestamp_zero_stays_idle);
    RUN_TEST(test_idle_remote_block_nonce_out_of_window_stays_idle);
    RUN_TEST(test_idle_remote_block_nonce_boundary_exact_window);
    RUN_TEST(test_idle_cancel_command_stays_idle);
    RUN_TEST(test_idle_unlock_command_stays_idle);
    RUN_TEST(test_idle_outputs_all_false);

    /* Group 3: THEFT_CONFIRMED state */
    RUN_TEST(test_theft_confirmed_outputs_alerts);
    RUN_TEST(test_theft_confirmed_no_starter_lock);
    RUN_TEST(test_theft_confirmed_cancel_returns_idle);
    RUN_TEST(test_theft_confirmed_window_not_expired_stays);
    RUN_TEST(test_theft_confirmed_window_expired_goes_blocking);
    RUN_TEST(test_theft_confirmed_window_expired_plus_one);
    RUN_TEST(test_theft_confirmed_block_command_does_not_retrip);
    RUN_TEST(test_theft_confirmed_window_exactly_60s);

    /* Group 4: BLOCKING state */
    RUN_TEST(test_blocking_vehicle_moving_applies_derating);
    RUN_TEST(test_blocking_vehicle_slow_derating_sets_derate_step);
    RUN_TEST(test_blocking_vehicle_moving_fast_derating_step_up);
    RUN_TEST(test_blocking_vehicle_slow_first_stop_sets_timestamp);
    RUN_TEST(test_blocking_stopped_hold_not_complete_stays);
    RUN_TEST(test_blocking_stopped_hold_complete_goes_blocked);
    RUN_TEST(test_blocking_already_stopped_does_not_reset_timestamp);
    RUN_TEST(test_blocking_moving_resets_stopped_timestamp);
    RUN_TEST(test_apply_derating_speed_above_safe_increments);
    RUN_TEST(test_apply_derating_speed_below_safe_sets_min);
    RUN_TEST(test_blocking_alerts_always_active);
    RUN_TEST(test_blocking_no_starter_lock_while_moving);
    RUN_TEST(test_blocking_speed_exactly_at_lock_limit_stops);
    RUN_TEST(test_blocking_speed_between_safe_and_lock);
    RUN_TEST(test_blocking_cancel_ignored);
    RUN_TEST(test_blocking_stop_hold_exactly_120s);

    /* Group 5: BLOCKED state */
    RUN_TEST(test_blocked_starter_lock_always_set);
    RUN_TEST(test_blocked_no_acoustic_alert);
    RUN_TEST(test_blocked_retransmit_on_interval);
    RUN_TEST(test_blocked_no_retransmit_before_interval);
    RUN_TEST(test_blocked_retransmit_updates_timestamp);
    RUN_TEST(test_blocked_retransmit_at_exact_5s_interval);
    RUN_TEST(test_blocked_valid_unlock_returns_idle);
    RUN_TEST(test_blocked_invalid_unlock_increments_counter);
    RUN_TEST(test_blocked_unlock_no_tcu_ack_rejected);
    RUN_TEST(test_blocked_multiple_invalid_unlocks_accumulate);

    /* Group 6: Default (invalid state) */
    RUN_TEST(test_invalid_state_resets_to_idle);
    RUN_TEST(test_invalid_state_255_resets_to_idle);

    /* Group 7: reb_security_validate_remote_command */
    RUN_TEST(test_security_valid_nonce_passes);
    RUN_TEST(test_security_nonce_equal_last_rejected);
    RUN_TEST(test_security_nonce_replay_rejected);
    RUN_TEST(test_security_command_none_rejected);
    RUN_TEST(test_security_timestamp_zero_rejected);
    RUN_TEST(test_security_nonce_out_of_window_rejected);
    RUN_TEST(test_security_nonce_exactly_window_size_accepted);
    RUN_TEST(test_security_nonce_window_size_plus_one_rejected);
    RUN_TEST(test_security_nonce_history_circular_overflow);
    RUN_TEST(test_security_nonce_stored_in_history);
    RUN_TEST(test_security_multiple_sequential_nonces_accepted);
    RUN_TEST(test_security_nonce_exactly_one_ahead_accepted);
    RUN_TEST(test_security_nonce_window_minus_one_accepted);
    RUN_TEST(test_security_cancel_command_validates);
    RUN_TEST(test_security_updates_last_valid_nonce_on_success);
    RUN_TEST(test_security_does_not_update_nonce_on_failure);
    RUN_TEST(test_security_nonce_zero_in_history_is_replay);

    /* Group 8: reb_security_unlock_allowed */
    RUN_TEST(test_unlock_allowed_valid_command_and_ack);
    RUN_TEST(test_unlock_allowed_invalid_command_rejected);
    RUN_TEST(test_unlock_allowed_no_tcu_ack_rejected);
    RUN_TEST(test_unlock_allowed_invalid_and_no_ack);
    RUN_TEST(test_unlock_allowed_increments_invalid_counter);

    /* Group 9: reb_rules_safe_to_block */
    RUN_TEST(test_rules_safe_speed_ok_rpm_ok);
    RUN_TEST(test_rules_safe_speed_ok_rpm_exact_limit);
    RUN_TEST(test_rules_safe_speed_too_high);
    RUN_TEST(test_rules_safe_rpm_too_high);
    RUN_TEST(test_rules_safe_both_at_boundary);
    RUN_TEST(test_rules_safe_speed_zero_rpm_zero);
    RUN_TEST(test_rules_both_exceed_limits);

    /* Group 10: reb_core_init */
    RUN_TEST(test_core_init_first_boot_initializes_state_machine);
    RUN_TEST(test_core_init_with_saved_state_loads_context);

    /* Group 11: reb_core_execute */
    RUN_TEST(test_core_execute_calls_state_machine);
    RUN_TEST(test_core_execute_saves_state);
    RUN_TEST(test_core_execute_full_block_cycle);

    /* Group 12: Boundary conditions */
    RUN_TEST(test_idle_intrusion_only_no_block_cmd);
    RUN_TEST(test_idle_block_cmd_valid_nonce_no_intrusion);
    RUN_TEST(test_idle_block_cmd_invalid_nonce_stays_idle);
    RUN_TEST(test_blocked_unlock_cmd_not_allowed_stays_blocked);
    RUN_TEST(test_blocked_no_cmd_stays_blocked);
    RUN_TEST(test_derating_clamped_at_max);
    RUN_TEST(test_core_execute_save_fails_gracefully);
    RUN_TEST(test_idle_two_consecutive_valid_blocks);

    /* Group 13: reb_config.h — constant validation */
    RUN_TEST(test_config_theft_confirm_window_ms);
    RUN_TEST(test_config_stop_hold_time_ms);
    RUN_TEST(test_config_blocked_retransmit_ms);
    RUN_TEST(test_config_max_allowed_speed_for_lock);
    RUN_TEST(test_config_safe_moving_speed_kmh);
    RUN_TEST(test_config_max_speed_for_block_kmh);
    RUN_TEST(test_config_derate_step_percent);
    RUN_TEST(test_config_derate_max_percent);
    RUN_TEST(test_config_derate_min_percent);
    RUN_TEST(test_config_max_invalid_attempts);
    RUN_TEST(test_config_nonce_window_size);
    RUN_TEST(test_config_nonce_history_size);
    RUN_TEST(test_config_min_battery_voltage);
    RUN_TEST(test_config_engine_rpm_limit);
    RUN_TEST(test_config_derate_max_greater_than_min);
    RUN_TEST(test_config_derate_step_fits_in_max);
    RUN_TEST(test_config_nonce_window_geq_history);

    /* Group 14: reb_types.h — enumerations, struct sizes, and field accessibility */
    RUN_TEST(test_types_reb_state_idle_is_zero);
    RUN_TEST(test_types_reb_state_sequence);
    RUN_TEST(test_types_remote_command_none_is_zero);
    RUN_TEST(test_types_remote_command_sequence);
    RUN_TEST(test_types_context_size_nonzero);
    RUN_TEST(test_types_inputs_size_nonzero);
    RUN_TEST(test_types_outputs_size_nonzero);
    RUN_TEST(test_types_context_nonce_history_field_size);
    RUN_TEST(test_types_inputs_all_fields_accessible);
    RUN_TEST(test_types_outputs_all_fields_accessible);
    RUN_TEST(test_types_context_initial_state_field);

    /* Group 15: reb_logger — interface robustness */
    RUN_TEST(test_logger_info_does_not_crash);
    RUN_TEST(test_logger_warn_does_not_crash);
    RUN_TEST(test_logger_error_does_not_crash);
    RUN_TEST(test_logger_info_empty_string);
    RUN_TEST(test_logger_warn_empty_string);
    RUN_TEST(test_logger_error_empty_string);
    RUN_TEST(test_logger_multiple_calls_same_level);
    RUN_TEST(test_logger_interleaved_levels);

    /* Group 16: reb_persistence (NFR-REL-001) */
    RUN_TEST(test_persistence_load_blocked_state_restores);
    RUN_TEST(test_persistence_load_blocking_state_restores);
    RUN_TEST(test_persistence_load_restores_invalid_attempts);
    RUN_TEST(test_persistence_load_restores_nonce_history);
    RUN_TEST(test_persistence_failed_load_triggers_fresh_init);
    RUN_TEST(test_persistence_load_corrupted_crc_returns_false);
    RUN_TEST(test_logger_fopen_fails_no_crash);
    RUN_TEST(test_persistence_save_called_on_execute);
    RUN_TEST(test_persistence_blocked_state_saved_and_resumed);
    RUN_TEST(test_persistence_load_sets_persisted_state_valid_flag);
    RUN_TEST(test_persistence_multiple_saves_last_wins);

    /* Group 17: reb_security — additional coverage */
    RUN_TEST(test_security_unlock_with_valid_nonce_passes_validate);
    RUN_TEST(test_security_nonce_history_does_not_block_new_nonces);
    RUN_TEST(test_security_replay_after_window_wrap);
    RUN_TEST(test_security_validate_increments_nonce_index);
    RUN_TEST(test_security_unlock_allowed_with_zero_nonce_rejected);

    /* Group 18: reb_state_machine — additional branch coverage */
    RUN_TEST(test_sm_full_cycle_with_unlock);
    RUN_TEST(test_sm_blocking_derating_never_below_min_while_slow);
    RUN_TEST(test_sm_blocking_derating_never_below_step_while_fast);
    RUN_TEST(test_sm_blocked_valid_unlock_resets_all_timers);
    RUN_TEST(test_sm_blocking_stopped_ts_not_set_when_moving);
    RUN_TEST(test_sm_theft_confirmed_stores_timestamp);
    RUN_TEST(test_sm_idle_intrusion_and_valid_block_both_trigger);
    RUN_TEST(test_sm_blocked_retransmit_then_no_retransmit);
    RUN_TEST(test_sm_outputs_cleared_each_step);

    return UNITY_END();
}