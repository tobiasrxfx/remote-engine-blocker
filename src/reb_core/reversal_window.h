/**
 * @file    reversal_window.h
 * @brief   REB — Janelas de Reversão FR-008 (60s) e FR-012 (90s)
 *
 * FR-008: 60 s para cancelamento após trigger SOURCE_AUTO.
 * FR-012: 90 s de aviso pré-bloqueio para trigger automático.
 *
 * Para SOURCE_PANEL ou SOURCE_REMOTE: janelas NÃO se aplicam.
 */

#ifndef REVERSAL_WINDOW_H
#define REVERSAL_WINDOW_H

#include "reb/reb_types.h"
#include <stdbool.h>
#include <stdint.h>

void rw_init(rw_ctx_t *ctx);

/**
 * @brief  Inicia uma janela de reversão.
 * @param  ctx   Contexto
 * @param  mode  RW_MODE_60 (FR-008) ou RW_MODE_90 (FR-012)
 */
void rw_start(rw_ctx_t *ctx, rw_mode_t mode);

/**
 * @brief  Executa um ciclo da janela. Deve ser chamada a cada Ts.
 * @param  ctx            Contexto
 * @param  password_valid Senha de cancelamento validada
 * @return RW_RUNNING, RW_ABORT ou RW_EXPIRE
 */
rw_result_t rw_step(rw_ctx_t *ctx, bool password_valid);

void rw_cancel(rw_ctx_t *ctx);

/**
 * @brief Marca que uma atuação de bloqueio já foi emitida.
 *        Após isso, cancelamento por senha é rejeitado (FR-012 §C2).
 */
void rw_set_actuation_issued(rw_ctx_t *ctx);

bool rw_is_actuation_issued(const rw_ctx_t *ctx);

/**
 * @brief Retorna segundos restantes da janela (para IHM countdown).
 */
uint32_t rw_remaining_s(const rw_ctx_t *ctx);

#endif /* REVERSAL_WINDOW_H */
