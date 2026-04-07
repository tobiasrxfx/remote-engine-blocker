/**
 * @file    panel_auth.c
 * @brief   REB — Implementação da Autenticação do Painel (FR-004)
 *
 * Comparação em tempo constante evita timing attacks (SAE J3061).
 * Lockout após MAX_AUTH_ATTEMPTS falhas por LOCKOUT_DURATION_S.
 */

#include "panel_auth.h"
#include "reb/reb_params.h"
#include <string.h>

void panel_auth_init(panel_auth_ctx_t *ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
}

/**
 * Comparação em tempo constante (evita timing attacks).
 * Compara dois uint32_t sem curto-circuito.
 */
static bool const_time_eq_u32(uint32_t a, uint32_t b)
{
    uint32_t diff = a ^ b;
    /* Fold: se qualquer bit diferir, diff != 0 */
    diff |= (diff >> 16U);
    diff |= (diff >> 8U);
    diff |= (diff >> 4U);
    diff |= (diff >> 2U);
    diff |= (diff >> 1U);
    return ((diff & 1U) == 0U);
}

void panel_auth_step(panel_auth_ctx_t *ctx,
                     bool auth_pulse,
                     uint32_t password_hash,
                     bool cancel_req,
                     bool *auth_ok_out,
                     bool *locked_out)
{
    bool rising_edge;

    *auth_ok_out = false;

    /* Decrementa lockout timer se ativo */
    if (ctx->lockout_active) {
        if (ctx->lockout_timer > 0U) {
            ctx->lockout_timer--;
        } else {
            ctx->lockout_active = false;
            ctx->wrong_cnt      = 0U;
        }
        *locked_out = ctx->lockout_active;
        ctx->prev_auth_pulse = auth_pulse;
        return;
    }

    /* Detecção de borda de subida no pulso de autenticação */
    rising_edge = (auth_pulse && !ctx->prev_auth_pulse);
    ctx->prev_auth_pulse = auth_pulse;

    if (rising_edge) {
        if (const_time_eq_u32(password_hash, (uint32_t)PANEL_PASSWORD_HASH)) {
            ctx->wrong_cnt  = 0U;
            ctx->auth_ok    = true;
            *auth_ok_out    = true;
        } else {
            ctx->wrong_cnt++;
            ctx->auth_ok = false;
            if (ctx->wrong_cnt >= (uint8_t)MAX_AUTH_ATTEMPTS) {
                ctx->lockout_active = true;
                ctx->lockout_timer  = (uint32_t)LOCKOUT_CYCLES;
                ctx->wrong_cnt      = (uint8_t)MAX_AUTH_ATTEMPTS; /* satura */
            }
        }
    }

    /* Cancelamento por painel não requer senha (apenas sinaliza) */
    (void)cancel_req;

    *locked_out = ctx->lockout_active;
}

void panel_auth_reset_lockout(panel_auth_ctx_t *ctx)
{
    ctx->lockout_active = false;
    ctx->lockout_timer  = 0U;
    ctx->wrong_cnt      = 0U;
}
