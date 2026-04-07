/**
 * @file    starter_control.c
 * @brief   REB — Implementação do Controle de Partida (FR-011)
 *
 * Ao entrar em STATE_BLOCKED: ativa inibição imediatamente.
 * A cada RETRANSMIT_BLOCK_TIMEOUT_CYCLES (500 ciclos = 5s):
 *   sinaliza cmd_pending=true para a camada CAN retransmitir 0x400.
 */

#include "starter_control.h"
#include "actuator_iface.h"
#include "reb/reb_params.h"
#include <string.h>

void starter_init(starter_ctx_t *ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
}

void starter_step(starter_ctx_t *ctx,
                  reb_state_t state,
                  actuator_output_t *out)
{
    if (state == STATE_BLOCKED) {
        if (!ctx->inhibit_active) {
            /* Primeira entrada em BLOCKED: ativa inibição imediatamente */
            ctx->inhibit_active   = true;
            ctx->retransmit_timer = 0U;
            ctx->cmd_pending      = true;
        } else {
            /* Retransmissão periódica a cada 5s (FR-011) */
            ctx->retransmit_timer++;
            if (ctx->retransmit_timer >= (uint32_t)RETRANSMIT_BLOCK_TIMEOUT_CYCLES) {
                ctx->retransmit_timer = 0U;
                ctx->cmd_pending      = true;
            } else {
                ctx->cmd_pending = false;
            }
        }
        act_set_starter_inhibit(true, out);
    } else {
        /* Fora de BLOCKED: libera partida */
        if (ctx->inhibit_active) {
            ctx->inhibit_active   = false;
            ctx->retransmit_timer = 0U;
            ctx->cmd_pending      = false;
            act_set_starter_inhibit(false, out);
        }
    }
}

void starter_release(starter_ctx_t *ctx, actuator_output_t *out)
{
    ctx->inhibit_active   = false;
    ctx->retransmit_timer = 0U;
    ctx->cmd_pending      = false;
    act_set_starter_inhibit(false, out);
}
