/**
 * @file    panel_auth.h
 * @brief   REB — Autenticação do Painel Físico (FR-004, NFR-SEC-001)
 */

#ifndef PANEL_AUTH_H
#define PANEL_AUTH_H

#include "reb/reb_types.h"
#include <stdbool.h>
#include <stdint.h>

void panel_auth_init(panel_auth_ctx_t *ctx);

/**
 * @brief  Processa tentativa de autenticação do painel.
 * @param  ctx           Contexto
 * @param  auth_pulse    Pulso de autenticação (borda detectable)
 * @param  password_hash Hash da senha tentada
 * @param  cancel_req    Solicitação de cancelamento
 * @param  auth_ok_out   TRUE se autenticado com sucesso
 * @param  locked_out    TRUE se painel em lockout
 */
void panel_auth_step(panel_auth_ctx_t *ctx,
                     bool auth_pulse,
                     uint32_t password_hash,
                     bool cancel_req,
                     bool *auth_ok_out,
                     bool *locked_out);

void panel_auth_reset_lockout(panel_auth_ctx_t *ctx);

#endif /* PANEL_AUTH_H */
