/**
 * @file    starter_control.h
 * @brief   REB — Controle de Inibição de Partida (FR-011, IF-CAN-005)
 *
 * Gerencia a transmissão periódica do comando REB_PREVENT_START (0x403/0x400)
 * com retransmissão a cada 5s enquanto em STATE_BLOCKED.
 */

#ifndef STARTER_CONTROL_H
#define STARTER_CONTROL_H

#include "reb/reb_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     inhibit_active;     /**< Inibição ativa                        */
    uint32_t retransmit_timer;   /**< Ciclos até próxima retransmissão      */
    bool     cmd_pending;        /**< Comando pendente de envio             */
} starter_ctx_t;

void starter_init(starter_ctx_t *ctx);

/**
 * @brief  Executa um ciclo do controle de partida.
 * @param  ctx    Contexto
 * @param  state  Estado atual da FSM
 * @param  out    Saídas do atuador (starter_ok atualizado aqui)
 */
void starter_step(starter_ctx_t *ctx,
                  reb_state_t state,
                  actuator_output_t *out);

void starter_release(starter_ctx_t *ctx, actuator_output_t *out);

#endif /* STARTER_CONTROL_H */
