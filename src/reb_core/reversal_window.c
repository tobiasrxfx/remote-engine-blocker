/**
 * @file    reversal_window.c
 * @brief   REB — Implementação das Janelas de Reversão (FR-008, FR-012)
 */

#include "reversal_window.h"
#include "reb/reb_params.h"
#include <string.h>

void rw_init(rw_ctx_t *ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
}

void rw_start(rw_ctx_t *ctx, rw_mode_t mode)
{
    ctx->mode        = mode;
    ctx->timer_cycles = 0U;
    ctx->active      = true;
    ctx->pre_block_alert_active  = true;
    ctx->blocking_actuation_issued = false;

    if (mode == RW_MODE_60) {
        ctx->limit_cycles = (uint32_t)T_PREALERT_CYCLES;  /* 6000 ciclos */
    } else {
        ctx->limit_cycles = (uint32_t)T_REVERSAL_CYCLES;  /* 9000 ciclos */
    }
}

rw_result_t rw_step(rw_ctx_t *ctx, bool password_valid)
{
    if (!ctx->active) {
        return RW_RUNNING;
    }

    ctx->timer_cycles++;

    /* Verificação de cancelamento por senha */
    if (password_valid) {
        /* FR-012 §C2: se atuação já emitida, senha NÃO pode cancelar */
        if (!ctx->blocking_actuation_issued) {
            ctx->active = false;
            ctx->pre_block_alert_active = false;
            return RW_ABORT;
        }
        /* Atuação emitida → rejeita cancelamento, continua bloqueio */
    }

    /* Verificação de expiração */
    if (ctx->timer_cycles >= ctx->limit_cycles) {
        ctx->active = false;
        ctx->pre_block_alert_active = false;
        return RW_EXPIRE;
    }

    return RW_RUNNING;
}

void rw_cancel(rw_ctx_t *ctx)
{
    ctx->active = false;
    ctx->pre_block_alert_active = false;
    ctx->timer_cycles = 0U;
}

void rw_set_actuation_issued(rw_ctx_t *ctx)
{
    ctx->blocking_actuation_issued = true;
}

bool rw_is_actuation_issued(const rw_ctx_t *ctx)
{
    return ctx->blocking_actuation_issued;
}

uint32_t rw_remaining_s(const rw_ctx_t *ctx)
{
    uint32_t elapsed_ms;
    uint32_t total_ms;

    if (!ctx->active) {
        return 0U;
    }

    elapsed_ms = ctx->timer_cycles * (uint32_t)REB_TS_MS;
    total_ms   = ctx->limit_cycles  * (uint32_t)REB_TS_MS;

    if (elapsed_ms >= total_ms) {
        return 0U;
    }

    return (total_ms - elapsed_ms) / 1000U;
}
