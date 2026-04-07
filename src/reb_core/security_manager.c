/**
 * @file    security_manager.c
 * @brief   REB — Implementação do Security Manager (NFR-SEC-001)
 **/


#include "security_manager.h"
#include "reb/reb_params.h"
#include <string.h>

void sec_mgr_init(sec_ctx_t *ctx)
{
    ctx->last_nonce = 0U;
}

bool sec_mgr_verify(sec_ctx_t *ctx,
                    uint16_t nonce,
                    uint32_t timestamp_ms,
                    bool sig_ok,
                    uint32_t current_ms,
                    auth_fail_t *result)
{
    uint32_t delta_ms;

    /* Passo 1: verificação de assinatura (flag do frame 0x200, simulada) */
    if (!sig_ok) {
        *result = AUTH_SIG_INVALID;
        return false;
    }

    /* Passo 2: verificação de nonce monotônico (anti-replay) */
    if ((uint32_t)nonce <= ctx->last_nonce) {
        *result = AUTH_NONCE_REPLAY;
        return false;
    }

    /* Passo 3: verificação de timestamp (janela de 30 s = 30000 ms)*/
 
    if (current_ms >= timestamp_ms) {
        delta_ms = current_ms - timestamp_ms;
    } else {
        /* Overflow de uint32 — timestamp do futuro é inválido */
        delta_ms = NONCE_WINDOW_MS + 1U;
    }

    if (delta_ms > NONCE_WINDOW_MS) {
        *result = AUTH_TS_EXPIRED;
        return false;
    }

    /* Tudo ok — atualiza último nonce */
    ctx->last_nonce = (uint32_t)nonce;
    *result = AUTH_OK;
    return true;
}
