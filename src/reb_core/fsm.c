/**
 * @file    fsm.c
 * @brief   REB — Implementação da FSM Principal (FR-001 a FR-013)
 *
 * Estrutura: cada estado tem função entry_* e during_*.
 * Transições são executadas no dispatcher reb_fsm_step().
 * NVM é escrita a cada transição de estado (NFR-REL-001).
 */

#include "fsm.h"
#include "reb/reb_params.h"
#include <string.h>
#include <stdbool.h>

/* =========================================================================
 * Utilitários internos
 * ========================================================================= */

static void persist_state(reb_ctx_t *ctx)
{
    nvm_data_t nvm;
    nvm.last_state          = ctx->fsm.state;
    nvm.last_nonce          = ctx->sec.last_nonce;
    nvm.panel_wrong_cnt     = ctx->panel.wrong_cnt;
    nvm.lockout_active      = ctx->panel.lockout_active;
    nvm.lockout_remaining_s = ctx->panel.lockout_timer /
                              ((uint32_t)(1U / REB_TS_S));
    (void)nvm_write_state(&nvm);

    evlog_write(&ctx->log, ctx->fsm.sim_time_ms,
                EVT_NVM_WRITE,
                ctx->fsm.prev_state, ctx->fsm.state,
                (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
}

static void do_transition(reb_ctx_t *ctx, reb_state_t new_state)
{
    ctx->fsm.prev_state = ctx->fsm.state;
    ctx->fsm.state      = new_state;

    evlog_write(&ctx->log, ctx->fsm.sim_time_ms,
                EVT_STATE_TRANSITION,
                ctx->fsm.prev_state, new_state,
                (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);

    persist_state(ctx);
}

/* Verifica validade do sinal de velocidade (NFR-SAF-002) */
static bool speed_is_valid(const reb_inputs_t *in)
{
    return (in->speed_sig_status == SIG_VALID);
}

/* Retorna TRUE se o comando remoto passou por todas as verificações */
static bool check_remote_cmd(reb_ctx_t *ctx, const reb_inputs_t *in)
{
    auth_fail_t result;
    bool ok;

    ok = sec_mgr_verify(&ctx->sec,
                        in->cmd_nonce,
                        in->cmd_timestamp_ms,
                        in->cmd_sig_ok,
                        in->sim_time_ms,
                        &result);
    if (!ok) {
        evlog_write(&ctx->log, in->sim_time_ms,
                    EVT_AUTH_FAIL,
                    ctx->fsm.state, ctx->fsm.state,
                    (uint8_t)SOURCE_REMOTE, (uint8_t)result);
    }
    return ok;
}

/* Calcula derate progressivo: cai DERATE_RATE_PCT_S por segundo */
static uint8_t calc_derate_ramp(reb_ctx_t *ctx)
{
    float step = DERATE_RATE_PCT_S * REB_TS_S; /* %/ciclo */

    if (ctx->derate_ramp > step) {
        ctx->derate_ramp -= step;
    } else {
        ctx->derate_ramp = 0.0f;
    }

    if (ctx->derate_ramp < (float)FUEL_FLOOR_PCT) {
        ctx->derate_ramp = (float)FUEL_FLOOR_PCT;
    }

    return (uint8_t)ctx->derate_ramp;
}

/* =========================================================================
 * STATE: IDLE
 * ========================================================================= */

static void entry_idle(reb_ctx_t *ctx, reb_outputs_t *out)
{
    /* Reseta todos os sub-sistemas de bloqueio */
    ctx->fsm.parked_timer   = 0U;
    ctx->derate_ramp        = (float)DERATE_PCT_INIT;
    ctx->fsm.unblock_requested = false;

    act_init(&ctx->act);
    starter_release(&ctx->starter, &ctx->act);
    alert_mgr_stop(&ctx->alert, NULL); /* alertas desativados */
    rw_init(&ctx->rw);
    sf_init(&ctx->sf);

    out->notify_theft   = false;
    out->notify_blocked = false;
    out->gps_send       = false;
    out->pre_block_alert = false;
    out->reversal_timer_s = 0U;
}

static void during_idle(reb_ctx_t *ctx,
                        const reb_inputs_t *in,
                        reb_outputs_t *out)
{
    sf_output_t sf_out;
    bool panel_auth_ok = false;
    bool panel_locked  = false;

    /* --- FR-004: Painel físico --- */
    panel_auth_step(&ctx->panel,
                    in->auth_manual_out,
                    in->password_attempt,
                    in->cancel_request,
                    &panel_auth_ok,
                    &panel_locked);

    if (panel_auth_ok) {
        evlog_write(&ctx->log, in->sim_time_ms, EVT_AUTH_OK,
                    STATE_IDLE, STATE_THEFT_CONFIRMED,
                    (uint8_t)SOURCE_PANEL, (uint8_t)AUTH_OK);
        ctx->fsm.source = SOURCE_PANEL;
        do_transition(ctx, STATE_THEFT_CONFIRMED);
        return;
    }

    /* --- FR-005: Bloqueio remoto via TCU (4G) --- */
    if (in->auth_blocked_remote && check_remote_cmd(ctx, in)) {
        ctx->fsm.source = SOURCE_REMOTE;
        do_transition(ctx, STATE_THEFT_CONFIRMED);
        return;
    }

    /* --- FR-006: Bloqueio remoto via SMS (fallback) ---
     *  No modelo, o canal SMS chega pelo mesmo frame 0x103/0x200
     *  com ip_rx_ok=0 e sms_rx_ok=1. A verificação de segurança é a mesma. */
    if (!in->ip_rx_ok && in->sms_rx_ok && in->auth_blocked_remote) {
        if (check_remote_cmd(ctx, in)) {
            ctx->fsm.source = SOURCE_REMOTE;
            do_transition(ctx, STATE_THEFT_CONFIRMED);
            return;
        }
    }

    /* --- FR-007: Detecção automática por sensores --- */
    sf_step(&ctx->sf, in->accel_peak, in->glass_break_flag, &sf_out);
    out->sensor_score = sf_out.theft_score;

    if (sf_out.theft_detected) {
        evlog_write(&ctx->log, in->sim_time_ms, EVT_SENSOR_THEFT,
                    STATE_IDLE, STATE_THEFT_CONFIRMED,
                    (uint8_t)SOURCE_AUTO, (uint8_t)AUTH_OK);
        ctx->fsm.source = SOURCE_AUTO;
        do_transition(ctx, STATE_THEFT_CONFIRMED);
        return;
    }

    (void)out;
}

/* =========================================================================
 * STATE: THEFT_CONFIRMED
 * ========================================================================= */

static void entry_theft_confirmed(reb_ctx_t *ctx, const reb_inputs_t *in,
                                   reb_outputs_t *out)
{
    /* Notifica TCU imediatamente na entrada (REB_STATUS 0x201) */
    out->notify_theft     = true;
    ctx->fsm.parked_timer = 0U;

    /* FR-008 / FR-012: janelas de reversão apenas para SOURCE_AUTO */
    if (ctx->fsm.source == SOURCE_AUTO) {
        rw_start(&ctx->rw, RW_MODE_90); /* FR-012: 90s de aviso */
    }

    evlog_write(&ctx->log, in->sim_time_ms, EVT_STATE_TRANSITION,
                STATE_IDLE, STATE_THEFT_CONFIRMED,
                (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
    (void)in;
}

static void during_theft_confirmed(reb_ctx_t *ctx,
                                   const reb_inputs_t *in,
                                   reb_outputs_t *out)
{
    rw_result_t rw_res;
    bool panel_auth_ok = false;
    bool panel_locked  = false;
    bool password_valid = false;
    sf_output_t sf_out;

    out->notify_theft  = true;
    out->gps_send      = true; /* Solicita GPS ao TCU (FR-014 stub) */

    /* --- Processar painel (senha de cancelamento) --- */
    panel_auth_step(&ctx->panel,
                    in->auth_manual_out,
                    in->password_attempt,
                    in->cancel_request,
                    &panel_auth_ok,
                    &panel_locked);
    password_valid = panel_auth_ok || in->cancel_request;

    /* --- FR-008/FR-012: janela de reversão (SOURCE_AUTO) --- */
    if (ctx->fsm.source == SOURCE_AUTO) {
        rw_res = rw_step(&ctx->rw, password_valid);
        out->pre_block_alert  = ctx->rw.pre_block_alert_active;
        out->reversal_timer_s = rw_remaining_s(&ctx->rw);

        if (rw_res == RW_ABORT) {
            /* Usuário cancelou dentro da janela */
            evlog_write(&ctx->log, in->sim_time_ms, EVT_REVERSAL_ABORT,
                        STATE_THEFT_CONFIRMED, STATE_IDLE,
                        (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
            alert_mgr_stop(&ctx->alert, NULL);
            do_transition(ctx, STATE_IDLE);
            entry_idle(ctx, out);
            return;
        }

        if (rw_res == RW_EXPIRE) {
            /* Janela expirou — avança para BLOCKING */
            evlog_write(&ctx->log, in->sim_time_ms, EVT_REVERSAL_EXPIRE,
                        STATE_THEFT_CONFIRMED, STATE_BLOCKING,
                        (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
            do_transition(ctx, STATE_BLOCKING);
            return;
        }

        /* Alerta visual/sonoro durante janela FR-013 */
        if (ctx->rw.pre_block_alert_active) {
            alert_mgr_start(&ctx->alert);
        }
    } else {
        /* SOURCE_PANEL ou SOURCE_REMOTE: avança direto para BLOCKING */
        /* Pequeno delay mínimo: espera t_min_after_confirmed */
        ctx->fsm.min_after_timer++;
        if (ctx->fsm.min_after_timer >= (uint32_t)T_MIN_AFTER_CYCLES) {
            do_transition(ctx, STATE_BLOCKING);
            return;
        }
    }

    /* --- FR-005: Desbloqueio remoto autenticado durante THEFT_CONFIRMED --- */
    if (in->remote_unblock_remote && check_remote_cmd(ctx, in)) {
        do_transition(ctx, STATE_IDLE);
        entry_idle(ctx, out);
        return;
    }

    /* Continua atualizando sensor score (para IHM) */
    sf_step(&ctx->sf, in->accel_peak, in->glass_break_flag, &sf_out);
    out->sensor_score = sf_out.theft_score;
}

/* =========================================================================
 * STATE: BLOCKING
 * ========================================================================= */

static void entry_blocking(reb_ctx_t *ctx)
{
    ctx->fsm.parked_timer = 0U;
    ctx->derate_ramp      = (float)DERATE_PCT_INIT;
    rw_set_actuation_issued(&ctx->rw);
}

static void during_blocking(reb_ctx_t *ctx,
                             const reb_inputs_t *in,
                             reb_outputs_t *out)
{
    uint8_t derate_raw;
    bool speed_valid;
    float spd;
    bool panel_auth_ok = false;
    bool panel_locked  = false;

    speed_valid = speed_is_valid(in);
    spd         = in->vehicle_speed_kmh;

    /* NFR-SAF-002: sinal de velocidade inválido → inibir ações perigosas */
    if (!speed_valid) {
        evlog_write(&ctx->log, in->sim_time_ms, EVT_SIGNAL_FAULT,
                    STATE_BLOCKING, STATE_BLOCKING,
                    (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
        /* Mantém derating atual, não avança para BLOCKED */
        out->derate_pct = ctx->act.derate_pct;
        return;
    }

    /* --- FR-003/FR-009: Derating progressivo enquanto em movimento --- */
    derate_raw = calc_derate_ramp(ctx);
    act_apply_derate(STATE_BLOCKING, spd, derate_raw, &ctx->act);

    out->derate_pct          = ctx->act.derate_pct;
    out->fuel_derating_active = ctx->act.fuel_derating_active;

    /* --- FR-010/FR-011: Timer de parada segura ---
     * Veículo parado (v <= V_STOP_KMH) por T_PARKED_S contínuos */
    if (spd <= V_STOP_KMH) {
        ctx->fsm.parked_timer++;
        if (ctx->fsm.parked_timer >= (uint32_t)T_PARKED_CYCLES) {
            /* Transição para BLOCKED */
            evlog_write(&ctx->log, in->sim_time_ms, EVT_SPEED_SAFE_STOP,
                        STATE_BLOCKING, STATE_BLOCKED,
                        (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
            do_transition(ctx, STATE_BLOCKED);
            return;
        }
    } else {
        /* FR-011: reset do timer se velocidade aumentar */
        ctx->fsm.parked_timer = 0U;
    }

    /* Alertas ativos durante BLOCKING */
    alert_mgr_start(&ctx->alert);

    /* --- Desbloqueio remoto autenticado --- */
    if (in->remote_unblock_remote && check_remote_cmd(ctx, in)) {
        do_transition(ctx, STATE_IDLE);
        entry_idle(ctx, out);
        return;
    }

    /* --- Cancelamento pelo painel com senha (FR-008) --- */
    panel_auth_step(&ctx->panel,
                    in->auth_manual_out,
                    in->password_attempt,
                    in->cancel_request,
                    &panel_auth_ok,
                    &panel_locked);
    if (panel_auth_ok) {
        do_transition(ctx, STATE_IDLE);
        entry_idle(ctx, out);
        return;
    }

    out->notify_theft  = true;
    out->pre_block_alert = true;
}

/* =========================================================================
 * STATE: BLOCKED
 * ========================================================================= */

static void entry_blocked(reb_ctx_t *ctx, reb_outputs_t *out)
{
    /* FR-011: derating liberado, starter inhibit ativo */
    act_apply_derate(STATE_BLOCKED, 0.0f, (uint8_t)DERATE_PCT_INIT, &ctx->act);
    act_set_starter_inhibit(true, &ctx->act);

    out->derate_pct           = ctx->act.derate_pct;
    out->starter_ok           = ctx->act.starter_ok;
    out->fuel_derating_active = false;
    out->starter_inhibit_active = true;
    out->notify_blocked       = true;
    out->gps_send             = true;

    evlog_write(&ctx->log, 0U, EVT_STARTER_INHIBIT,
                STATE_BLOCKING, STATE_BLOCKED,
                (uint8_t)ctx->fsm.source, (uint8_t)AUTH_OK);
}

static void during_blocked(reb_ctx_t *ctx,
                            const reb_inputs_t *in,
                            reb_outputs_t *out)
{
    /* Retransmissão periódica do comando de inibição (FR-011) */
    starter_step(&ctx->starter, STATE_BLOCKED, &ctx->act);

    out->derate_pct           = ctx->act.derate_pct;
    out->starter_ok           = ctx->act.starter_ok;
    out->fuel_derating_active = false;
    out->starter_inhibit_active = true;
    out->notify_blocked       = true;

    /* Alertas visuais/sonoros permanecem ativos */
    alert_mgr_start(&ctx->alert);

    /* --- Desbloqueio por comando remoto autenticado (TCU ACK necessário) --- */
    if (in->remote_unblock_remote && check_remote_cmd(ctx, in)) {
        if (in->tcu_ack) {
            evlog_write(&ctx->log, in->sim_time_ms, EVT_UNBLOCK,
                        STATE_BLOCKED, STATE_IDLE,
                        (uint8_t)SOURCE_REMOTE, (uint8_t)AUTH_OK);
            starter_release(&ctx->starter, &ctx->act);
            do_transition(ctx, STATE_IDLE);
            entry_idle(ctx, out);
            return;
        }
    }

    /* --- Desbloqueio por painel com senha válida --- */
    {
        bool panel_auth_ok = false;
        bool panel_locked  = false;
        panel_auth_step(&ctx->panel,
                        in->auth_manual_out,
                        in->password_attempt,
                        in->cancel_request,
                        &panel_auth_ok,
                        &panel_locked);
        if (panel_auth_ok) {
            evlog_write(&ctx->log, in->sim_time_ms, EVT_UNBLOCK,
                        STATE_BLOCKED, STATE_IDLE,
                        (uint8_t)SOURCE_PANEL, (uint8_t)AUTH_OK);
            starter_release(&ctx->starter, &ctx->act);
            do_transition(ctx, STATE_IDLE);
            entry_idle(ctx, out);
            return;
        }
    }
}

/* =========================================================================
 * API Pública
 * ========================================================================= */

void reb_fsm_init(reb_ctx_t *ctx)
{
    nvm_data_t nvm;
    nvm_result_t nvm_res;

    (void)memset(ctx, 0, sizeof(*ctx));

    sec_mgr_init(&ctx->sec);
    panel_auth_init(&ctx->panel);
    sf_init(&ctx->sf);
    act_init(&ctx->act);
    starter_init(&ctx->starter);
    alert_mgr_init(&ctx->alert);
    rw_init(&ctx->rw);
    evlog_init(&ctx->log);

    ctx->derate_ramp  = (float)DERATE_PCT_INIT;
    ctx->fsm.state    = STATE_IDLE;
    ctx->fsm.source   = SOURCE_REMOTE;

    /* NFR-REL-001: tenta restaurar estado do NVM após reset/power loss */
    nvm_res = nvm_read_state(&nvm);
    if (nvm_res == NVM_OK) {
        ctx->fsm.state          = nvm.last_state;
        ctx->sec.last_nonce     = nvm.last_nonce;
        ctx->panel.wrong_cnt    = nvm.panel_wrong_cnt;
        ctx->panel.lockout_active = nvm.lockout_active;
        ctx->fsm.nvm_state_loaded = true;

        evlog_write(&ctx->log, 0U, EVT_NVM_RESTORE,
                    STATE_IDLE, nvm.last_state,
                    (uint8_t)SOURCE_REMOTE, (uint8_t)AUTH_OK);

        /* Se restaurou BLOCKED, ativa starter inhibit imediatamente */
        if (ctx->fsm.state == STATE_BLOCKED) {
            act_set_starter_inhibit(true, &ctx->act);
            ctx->starter.inhibit_active = true;
        }
    }
}

void reb_fsm_step(reb_ctx_t *ctx,
                  const reb_inputs_t *in,
                  reb_outputs_t *out)
{
    alert_output_t alert_out;
    bool state_changed;

    /* Atualiza tempo de simulação */
    ctx->fsm.sim_time_ms = in->sim_time_ms;

    /* Prepara saídas com valores padrão */
    (void)memset(out, 0, sizeof(*out));
    out->derate_pct         = (uint8_t)DERATE_PCT_INIT;
    out->starter_ok         = !ctx->act.starter_inhibit_active;
    out->current_state      = ctx->fsm.state;
    out->nvm_state_loaded   = ctx->fsm.nvm_state_loaded;

    state_changed = false;

    /* Dispatcher de estados */
    switch (ctx->fsm.state) {
        case STATE_IDLE:
            during_idle(ctx, in, out);
            /* Detecta transição disparada durante during_idle */
            if (ctx->fsm.state == STATE_THEFT_CONFIRMED) {
                state_changed = true;
                entry_theft_confirmed(ctx, in, out);
            }
            break;

        case STATE_THEFT_CONFIRMED:
            during_theft_confirmed(ctx, in, out);
            if (ctx->fsm.state == STATE_BLOCKING) {
                state_changed = true;
                entry_blocking(ctx);
            }
            break;

        case STATE_BLOCKING:
            during_blocking(ctx, in, out);
            if (ctx->fsm.state == STATE_BLOCKED) {
                state_changed = true;
                entry_blocked(ctx, out);
            }
            break;

        case STATE_BLOCKED:
            during_blocked(ctx, in, out);
            break;

        default:
            /* Estado inválido — retorna a IDLE (fail-safe) */
            ctx->fsm.state = STATE_IDLE;
            break;
    }

    /* Atualiza alertas */
    (void)memset(&alert_out, 0, sizeof(alert_out));
    alert_mgr_step(&ctx->alert, &alert_out);
    out->alert_sonic  = alert_out.horn_active;
    out->alert_visual = alert_out.hazard_active;

    /* Copia saídas do atuador */
    out->derate_pct             = ctx->act.derate_pct;
    out->starter_ok             = ctx->act.starter_ok;
    out->fuel_derating_active   = ctx->act.fuel_derating_active;
    out->starter_inhibit_active = ctx->act.starter_inhibit_active;
    out->current_state          = ctx->fsm.state;

    /* Timer de status CAN (envia notify a cada STATUS_PERIOD_CYCLES) */
    ctx->status_timer++;
    if (ctx->status_timer >= (uint32_t)STATUS_PERIOD_CYCLES) {
        ctx->status_timer = 0U;
    }

    (void)state_changed;
}
