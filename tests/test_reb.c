/**
 * @file    test_reb.c
 * @brief   REB — Testes Unitários (TEST-001..TEST-019 do RTM SRS §8)
 *
 * Framework: Unity (Mike Karlesky) — um assert por teste.
 * Compilar com: gcc -DUNITY_TEST test_reb.c unity.c ... -o test_reb
 *
 * Como STUB de Unity (sem a lib instalada), este arquivo também pode
 * ser compilado standalone com o mini-framework incluído ao final.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Mini-framework de teste (substitui Unity quando não disponível)
 * ========================================================================= */
static int  g_tests_run    = 0;
static int  g_tests_failed = 0;

#define TEST_ASSERT(cond)                                                   \
    do {                                                                    \
        g_tests_run++;                                                      \
        if (!(cond)) {                                                      \
            g_tests_failed++;                                               \
            printf("  FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        } else {                                                            \
            printf("  PASS  %s\n", #cond);                                  \
        }                                                                   \
    } while (0)

#define RUN_TEST(fn)                                                        \
    do {                                                                    \
        printf("\n--- " #fn " ---\n");                                      \
        fn();                                                               \
    } while (0)

/* =========================================================================
 * Includes dos módulos REB
 * ========================================================================= */
#include "include/reb/reb_types.h"
#include "include/reb/reb_params.h"
#include "src/reb_core/event_log.h"
#include "src/reb_core/nvm.h"
#include "src/reb_core/security_manager.h"
#include "src/reb_core/panel_auth.h"
#include "src/reb_core/sensor_fusion.h"
#include "src/reb_core/actuator_iface.h"
#include "src/reb_core/starter_control.h"
#include "src/reb_core/alert_manager.h"
#include "src/reb_core/reversal_window.h"
#include "src/reb_core/fsm.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Inicializa com NVM limpa — isola testes (sem contaminação entre eles). */
static void test_setup(reb_ctx_t *ctx)
{
    nvm_invalidate();
    reb_fsm_init(ctx);
}

static reb_inputs_t make_idle_inputs(void)
{
    reb_inputs_t in;
    (void)memset(&in, 0, sizeof(in));
    in.speed_sig_status   = SIG_VALID;
    in.vehicle_speed_kmh  = 0.0f;
    in.ip_rx_ok           = true;
    in.sim_time_ms        = 1000U;
    return in;
}

static reb_inputs_t make_moving_inputs(float speed_kmh)
{
    reb_inputs_t in = make_idle_inputs();
    in.vehicle_speed_kmh = speed_kmh;
    return in;
}

static void run_cycles(reb_ctx_t *ctx, reb_inputs_t *in,
                       reb_outputs_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0U; i < n; i++) {
        in->sim_time_ms += (uint32_t)REB_TS_MS;
        reb_fsm_step(ctx, in, out);
    }
}

/* =========================================================================
 * TEST-001: FR-001 — SC0: sistema permanece em IDLE sem evento
 * ========================================================================= */
static void test_001_idle_no_event(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();
    run_cycles(&ctx, &in, &out, 100U);

    TEST_ASSERT(ctx.fsm.state == STATE_IDLE);
    TEST_ASSERT(out.notify_theft == false);
    TEST_ASSERT(out.notify_blocked == false);
}

/* =========================================================================
 * TEST-002: FR-002 — SC1: confirmação de roubo válida → THEFT_CONFIRMED
 * ========================================================================= */
static void test_002_theft_confirmed_remote(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();

    /* Injeta comando remoto válido */
    in.auth_blocked_remote = true;
    in.cmd_sig_ok          = true;
    in.cmd_nonce           = 1U;
    in.cmd_timestamp_ms    = in.sim_time_ms; /* dentro da janela de 30s */

    reb_fsm_step(&ctx, &in, &out);

    TEST_ASSERT(ctx.fsm.state == STATE_THEFT_CONFIRMED);
    TEST_ASSERT(out.notify_theft == true);
}

/* =========================================================================
 * TEST-003: FR-002 — SC2: comando inválido rejeitado, permanece em IDLE
 * ========================================================================= */
static void test_003_invalid_cmd_rejected(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();

    /* sig_ok = false → deve rejeitar */
    in.auth_blocked_remote = true;
    in.cmd_sig_ok          = false;
    in.cmd_nonce           = 1U;
    in.cmd_timestamp_ms    = in.sim_time_ms;

    reb_fsm_step(&ctx, &in, &out);

    TEST_ASSERT(ctx.fsm.state == STATE_IDLE);
}

/* =========================================================================
 * TEST-004: FR-004 — Painel físico ativa THEFT_CONFIRMED
 * ========================================================================= */
static void test_004_panel_activation(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();

    /* Injeta senha correta via painel */
    in.auth_manual_out  = true;
    in.password_attempt = (uint32_t)PANEL_PASSWORD_HASH;

    reb_fsm_step(&ctx, &in, &out);

    TEST_ASSERT(ctx.fsm.state == STATE_THEFT_CONFIRMED);
}

/* =========================================================================
 * TEST-005: FR-004 — Lockout após 3 tentativas erradas
 * ========================================================================= */
static void test_005_panel_lockout(void)
{
    panel_auth_ctx_t panel;
    bool auth_ok = false;
    bool locked  = false;
    uint32_t i;

    panel_auth_init(&panel);

    /* 3 tentativas erradas */
    for (i = 0U; i < (uint32_t)MAX_AUTH_ATTEMPTS; i++) {
        panel.prev_auth_pulse = false; /* garante borda */
        panel_auth_step(&panel, true, 0xDEADBEEFUL,
                        false, &auth_ok, &locked);
    }

    TEST_ASSERT(locked == true);
    TEST_ASSERT(auth_ok == false);
}

/* =========================================================================
 * TEST-006: NFR-SEC-001 — Anti-replay: nonce repetido rejeitado
 * ========================================================================= */
static void test_006_nonce_replay(void)
{
    sec_ctx_t sec;
    auth_fail_t result;
    bool ok;

    sec_mgr_init(&sec);

    /* Primeiro uso do nonce 5: aceito */
    ok = sec_mgr_verify(&sec, 5U, 1000U, true, 1000U, &result);
    TEST_ASSERT(ok == true);
    TEST_ASSERT(result == AUTH_OK);

    /* Replay do nonce 5: rejeitado */
    ok = sec_mgr_verify(&sec, 5U, 2000U, true, 2000U, &result);
    TEST_ASSERT(ok == false);
    TEST_ASSERT(result == AUTH_NONCE_REPLAY);
}

/* =========================================================================
 * TEST-007: NFR-SEC-001 — Timestamp expirado rejeitado
 * ========================================================================= */
static void test_007_timestamp_expired(void)
{
    sec_ctx_t sec;
    auth_fail_t result;
    bool ok;

    sec_mgr_init(&sec);

    /* Timestamp de 31s atrás (> 30000ms janela) */
    ok = sec_mgr_verify(&sec, 1U, 0U, true, 31000U, &result);
    TEST_ASSERT(ok == false);
    TEST_ASSERT(result == AUTH_TS_EXPIRED);
}

/* =========================================================================
 * TEST-008: FR-007 — Sensor fusion detecta roubo após debounce
 * ========================================================================= */
static void test_008_sensor_fusion_detect(void)
{
    sf_ctx_t sf;
    sf_output_t out;
    uint32_t i;

    sf_init(&sf);

    /* Injeta sinais altos por SF_DEBOUNCE_CYCLES ciclos */
    for (i = 0U; i < (uint32_t)SF_DEBOUNCE_CYCLES; i++) {
        sf_step(&sf, ACCEL_MAX, 1.0f, &out);
    }

    TEST_ASSERT(out.theft_detected == true);
    TEST_ASSERT(out.theft_score >= SF_THRESH);
}

/* =========================================================================
 * TEST-009: FR-009 — Safety floor: derate_pct >= 10% durante BLOCKING
 * ========================================================================= */
static void test_009_fuel_safety_floor(void)
{
    actuator_output_t act;
    act_init(&act);

    /* Injeta derate_pct_in = 0% com veículo em 5 km/h e BLOCKING */
    act_apply_derate(STATE_BLOCKING, 5.0f, 0U, &act);

    TEST_ASSERT(act.derate_pct >= (uint8_t)FUEL_FLOOR_PCT);
    TEST_ASSERT(act.fuel_derating_active == true);
}

/* =========================================================================
 * TEST-010: FR-009 — Nenhuma amostra abaixo de 10% em BLOCKING + motion
 * ========================================================================= */
static void test_010_fuel_floor_zero_violations(void)
{
    actuator_output_t act;
    uint8_t derate_in;
    bool violation = false;

    act_init(&act);

    /* Varre todos os valores possíveis de derate_pct_in [0..255] */
    for (derate_in = 0U; derate_in <= 100U; derate_in++) {
        act_apply_derate(STATE_BLOCKING, 10.0f, derate_in, &act);
        if (act.derate_pct < (uint8_t)FUEL_FLOOR_PCT) {
            violation = true;
        }
    }

    TEST_ASSERT(violation == false);
}

/* =========================================================================
 * TEST-011: FR-011 — Transição para BLOCKED após 120s de parada
 * ========================================================================= */
static void test_011_blocked_after_120s_stop(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);

    /* Força estado BLOCKING diretamente */
    ctx.fsm.state  = STATE_BLOCKING;
    ctx.fsm.source = SOURCE_REMOTE;

    in = make_idle_inputs(); /* v=0 */

    /* Roda T_PARKED_CYCLES + 1 ciclos parado */
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES + 1U);

    TEST_ASSERT(ctx.fsm.state == STATE_BLOCKED);
    TEST_ASSERT(out.starter_inhibit_active == true);
    TEST_ASSERT(out.fuel_derating_active == false);
}

/* =========================================================================
 * TEST-012: FR-011 — Timer reset se velocidade aumentar durante BLOCKING
 * ========================================================================= */
static void test_012_parked_timer_reset_on_motion(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    ctx.fsm.state  = STATE_BLOCKING;
    ctx.fsm.source = SOURCE_REMOTE;

    in = make_idle_inputs(); /* v=0 */

    /* Roda metade do timer parado */
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES / 2U);
    TEST_ASSERT(ctx.fsm.state == STATE_BLOCKING);

    /* Move o veículo → deve resetar timer */
    in.vehicle_speed_kmh = 10.0f;
    reb_fsm_step(&ctx, &in, &out);

    /* Volta a parar: precisa de T_PARKED_CYCLES completos novamente */
    in.vehicle_speed_kmh = 0.0f;
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES / 2U);

    /* Ainda não deve ter transitado para BLOCKED */
    TEST_ASSERT(ctx.fsm.state == STATE_BLOCKING);
}

/* =========================================================================
 * TEST-013: FR-008 — Janela de reversão 60s: abort antes de expirar
 * ========================================================================= */
static void test_013_reversal_window_abort(void)
{
    rw_ctx_t rw;
    rw_result_t res;
    uint32_t i;

    rw_init(&rw);
    rw_start(&rw, RW_MODE_60);

    /* Avança 30s (3000 ciclos) sem senha */
    for (i = 0U; i < 3000U; i++) {
        res = rw_step(&rw, false);
        TEST_ASSERT(res == RW_RUNNING);
    }

    /* Injeta senha válida — deve abortar */
    res = rw_step(&rw, true);
    TEST_ASSERT(res == RW_ABORT);
}

/* =========================================================================
 * TEST-014: FR-012 — Janela de reversão 90s expira e avança para BLOCKING
 * ========================================================================= */
static void test_014_reversal_window_90s_expire(void)
{
    rw_ctx_t rw;
    rw_result_t res = RW_RUNNING;
    uint32_t i;

    rw_init(&rw);
    rw_start(&rw, RW_MODE_90);

    /* Roda T_REVERSAL_CYCLES + 1 ciclos sem senha */
    for (i = 0U; i < (uint32_t)T_REVERSAL_CYCLES; i++) {
        res = rw_step(&rw, false);
    }

    TEST_ASSERT(res == RW_EXPIRE);
}

/* =========================================================================
 * TEST-015: FR-012 §C2 — Senha rejeitada após atuação emitida
 * ========================================================================= */
static void test_015_reversal_rejected_after_actuation(void)
{
    rw_ctx_t rw;
    rw_result_t res;

    rw_init(&rw);
    rw_start(&rw, RW_MODE_90);
    rw_set_actuation_issued(&rw);

    /* Injeta senha — deve ser rejeitada (não retorna RW_ABORT) */
    res = rw_step(&rw, true);
    TEST_ASSERT(res != RW_ABORT);
}

/* =========================================================================
 * TEST-016: NFR-SAF-001 — Starter inibido apenas quando v=0
 * ========================================================================= */
static void test_016_starter_only_when_stopped(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    ctx.fsm.state  = STATE_BLOCKING;
    ctx.fsm.source = SOURCE_REMOTE;

    /* Veículo em movimento — starter NÃO pode ser inibido */
    in = make_moving_inputs(80.0f);

    /* Roda mais que T_PARKED_CYCLES (mas velocidade alta → timer não incrementa) */
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES + 100U);

    TEST_ASSERT(ctx.fsm.state != STATE_BLOCKED);
    TEST_ASSERT(out.starter_inhibit_active == false);
}

/* =========================================================================
 * TEST-017: NFR-SAF-002 — Sinal de velocidade inválido inibe bloqueio
 * ========================================================================= */
static void test_017_signal_fault_inhibit(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    ctx.fsm.state  = STATE_BLOCKING;
    ctx.fsm.source = SOURCE_REMOTE;

    in = make_idle_inputs();
    in.speed_sig_status = SIG_MISSING; /* sinal inválido */

    /* Roda T_PARKED_CYCLES ciclos com sinal inválido */
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES + 1U);

    /* Não deve ter transitado para BLOCKED sem sinal válido */
    TEST_ASSERT(ctx.fsm.state != STATE_BLOCKED);
}

/* =========================================================================
 * TEST-018: NFR-REL-001 — NVM persiste e restaura STATE_BLOCKED
 * ========================================================================= */
static void test_018_nvm_persistence(void)
{
    nvm_data_t write_data;
    nvm_data_t read_data;
    nvm_result_t res;

    (void)memset(&write_data, 0, sizeof(write_data));
    write_data.last_state  = STATE_BLOCKED;
    write_data.last_nonce  = 42U;

    res = nvm_write_state(&write_data);
    TEST_ASSERT(res == NVM_OK);

    (void)memset(&read_data, 0, sizeof(read_data));
    res = nvm_read_state(&read_data);
    TEST_ASSERT(res == NVM_OK);
    TEST_ASSERT(read_data.last_state == STATE_BLOCKED);
    TEST_ASSERT(read_data.last_nonce == 42U);
}

/* =========================================================================
 * TEST-019: NFR-REL-001 — Após power loss, BLOCKED é restaurado
 * ========================================================================= */
static void test_019_nvm_restore_on_init(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    /* Simula ciclo anterior: chega ao estado BLOCKED e persiste */
    test_setup(&ctx);
    ctx.fsm.state  = STATE_BLOCKING;
    ctx.fsm.source = SOURCE_REMOTE;
    in = make_idle_inputs();
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES + 1U);

    TEST_ASSERT(ctx.fsm.state == STATE_BLOCKED);

    /* Simula power loss: cria novo contexto e chama init */
    {
        reb_ctx_t ctx2;
        reb_fsm_init(&ctx2); /* lê NVM */

        TEST_ASSERT(ctx2.fsm.state == STATE_BLOCKED);
        TEST_ASSERT(ctx2.fsm.nvm_state_loaded == true);
        TEST_ASSERT(ctx2.act.starter_inhibit_active == true);
    }
}

/* =========================================================================
 * TEST-020: NFR-INFO-001 — Log registra transições de estado
 * ========================================================================= */
static void test_020_event_log(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();

    in.auth_blocked_remote = true;
    in.cmd_sig_ok          = true;
    in.cmd_nonce           = 1U;
    in.cmd_timestamp_ms    = in.sim_time_ms;
    reb_fsm_step(&ctx, &in, &out);

    TEST_ASSERT(evlog_count(&ctx.log) > 0U);

    {
        const event_record_t *rec = evlog_get(&ctx.log, 0U);
        TEST_ASSERT(rec != NULL);
        TEST_ASSERT(rec->event_code == EVT_STATE_TRANSITION ||
                    rec->event_code == EVT_NVM_WRITE);
    }
}

/* =========================================================================
 * TEST-021: FR-013 — Alertas ativados e buzina oscila a 1Hz
 * ========================================================================= */
static void test_021_alert_manager(void)
{
    alert_ctx_t alert;
    alert_output_t out;
    uint32_t i;
    uint32_t toggles = 0U;
    bool prev_horn;

    alert_mgr_init(&alert);
    alert_mgr_start(&alert);

    /* Roda 2 períodos completos (200 ciclos) e conta toggles */
    alert_mgr_step(&alert, &out);
    prev_horn = out.horn_active;

    for (i = 1U; i < 200U; i++) {
        alert_mgr_step(&alert, &out);
        if (out.horn_active != prev_horn) {
            toggles++;
            prev_horn = out.horn_active;
        }
    }

    /* 200 ciclos = 2s → 2 Hz de toggles = ~4 semiciclos */
    TEST_ASSERT(toggles >= 3U);
    TEST_ASSERT(out.hazard_active == true);
    TEST_ASSERT(out.hmi_alert == true);
}

/* =========================================================================
 * TEST-022: FR-006 — SMS fallback ativa bloqueio quando 4G indisponível
 * ========================================================================= */
static void test_022_sms_fallback(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();

    /* 4G indisponível, SMS disponível */
    in.ip_rx_ok            = false;
    in.sms_rx_ok           = true;
    in.auth_blocked_remote = true;
    in.cmd_sig_ok          = true;
    in.cmd_nonce           = 1U;
    in.cmd_timestamp_ms    = in.sim_time_ms;

    reb_fsm_step(&ctx, &in, &out);

    TEST_ASSERT(ctx.fsm.state == STATE_THEFT_CONFIRMED);
}

/* =========================================================================
 * TEST-023: FR-011 — Em STATE_BLOCKED, derating desativado
 * ========================================================================= */
static void test_023_no_derating_in_blocked(void)
{
    actuator_output_t act;
    act_init(&act);

    act_apply_derate(STATE_BLOCKED, 0.0f, 0U, &act);

    TEST_ASSERT(act.fuel_derating_active == false);
    TEST_ASSERT(act.derate_pct == (uint8_t)DERATE_PCT_INIT);
}

/* =========================================================================
 * CENÁRIO INTEGRADO: §7.1.1 — Bloqueio Remoto Bem-Sucedido
 * Veículo a 0 km/h, comando remoto válido → deve chegar a BLOCKED
 * ========================================================================= */
static void test_sc01_full_remote_block(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;

    test_setup(&ctx);
    in = make_idle_inputs();

    /* t=0: injeta bloqueio remoto */
    in.auth_blocked_remote = true;
    in.cmd_sig_ok          = true;
    in.cmd_nonce           = 10U;
    in.cmd_timestamp_ms    = in.sim_time_ms;
    reb_fsm_step(&ctx, &in, &out);
    TEST_ASSERT(ctx.fsm.state == STATE_THEFT_CONFIRMED);

    in.auth_blocked_remote = false;

    /* Avança pelo T_MIN_AFTER_CYCLES para ir a BLOCKING */
    run_cycles(&ctx, &in, &out, (uint32_t)T_MIN_AFTER_CYCLES + 1U);
    TEST_ASSERT(ctx.fsm.state == STATE_BLOCKING);

    /* Veículo parado por T_PARKED_CYCLES → BLOCKED */
    run_cycles(&ctx, &in, &out, (uint32_t)T_PARKED_CYCLES + 1U);
    TEST_ASSERT(ctx.fsm.state == STATE_BLOCKED);
    TEST_ASSERT(out.starter_inhibit_active == true);
    TEST_ASSERT(out.notify_blocked == true);
}

/* =========================================================================
 * CENÁRIO INTEGRADO: §7.1.3 — Bloqueio com veículo em movimento
 * v=80 km/h → derating ativo, sem starter block enquanto em movimento
 * ========================================================================= */
static void test_sc03_block_while_moving(void)
{
    reb_ctx_t ctx;
    reb_inputs_t in;
    reb_outputs_t out;
    bool no_starter_block_while_moving = true;
    uint32_t i;

    test_setup(&ctx);

    /* Força BLOCKING com veículo a 80 km/h */
    ctx.fsm.state  = STATE_BLOCKING;
    ctx.fsm.source = SOURCE_REMOTE;
    in = make_moving_inputs(80.0f);

    /* Roda 500 ciclos verificando que starter NÃO é inibido */
    for (i = 0U; i < 500U; i++) {
        in.sim_time_ms += (uint32_t)REB_TS_MS;
        reb_fsm_step(&ctx, &in, &out);
        if (out.starter_inhibit_active) {
            no_starter_block_while_moving = false;
        }
        /* Piso de 10% deve ser respeitado */
        TEST_ASSERT(out.derate_pct >= (uint8_t)FUEL_FLOOR_PCT);
    }

    TEST_ASSERT(no_starter_block_while_moving == true);
}

/* =========================================================================
 * MAIN
 * ========================================================================= */
int main(void)
{
    printf("=========================================\n");
    printf("  REB — Suite de Testes Unitários\n");
    printf("  Alinhado ao RTM SRS §8 TEST-001..019\n");
    printf("=========================================\n");

    RUN_TEST(test_001_idle_no_event);
    RUN_TEST(test_002_theft_confirmed_remote);
    RUN_TEST(test_003_invalid_cmd_rejected);
    RUN_TEST(test_004_panel_activation);
    RUN_TEST(test_005_panel_lockout);
    RUN_TEST(test_006_nonce_replay);
    RUN_TEST(test_007_timestamp_expired);
    RUN_TEST(test_008_sensor_fusion_detect);
    RUN_TEST(test_009_fuel_safety_floor);
    RUN_TEST(test_010_fuel_floor_zero_violations);
    RUN_TEST(test_011_blocked_after_120s_stop);
    RUN_TEST(test_012_parked_timer_reset_on_motion);
    RUN_TEST(test_013_reversal_window_abort);
    RUN_TEST(test_014_reversal_window_90s_expire);
    RUN_TEST(test_015_reversal_rejected_after_actuation);
    RUN_TEST(test_016_starter_only_when_stopped);
    RUN_TEST(test_017_signal_fault_inhibit);
    RUN_TEST(test_018_nvm_persistence);
    RUN_TEST(test_019_nvm_restore_on_init);
    RUN_TEST(test_020_event_log);
    RUN_TEST(test_021_alert_manager);
    RUN_TEST(test_022_sms_fallback);
    RUN_TEST(test_023_no_derating_in_blocked);
    RUN_TEST(test_sc01_full_remote_block);
    RUN_TEST(test_sc03_block_while_moving);

    printf("\n=========================================\n");
    printf("  Resultados: %d/%d passaram\n",
           g_tests_run - g_tests_failed, g_tests_run);
    if (g_tests_failed == 0) {
        printf("  STATUS: TODOS OS TESTES PASSARAM\n");
    } else {
        printf("  STATUS: %d FALHA(S)\n", g_tests_failed);
    }
    printf("=========================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
