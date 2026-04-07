/**
 * @file    reb_main.h
 * @brief   REB — Ponto de Integração Principal
 *
 * Este arquivo é o único que a dupla CAN e a dupla IHM precisam incluir.
 * Exporta:
 *   - reb_init(): chamada uma vez no startup
 *   - reb_step(): chamada a cada Ts=10ms com dados CAN decodificados
 *   - reb_get_state(): leitura assíncrona do estado (para IHM)
 */

#ifndef REB_MAIN_H
#define REB_MAIN_H

#include "reb/reb_types.h"
#include "src/reb_core/fsm.h"

/**
 * @brief  Inicializa o sistema REB completo.
 *         Deve ser chamada UMA vez antes do loop principal.
 */
void reb_init(reb_ctx_t *ctx);

/**
 * @brief  Executa um ciclo completo do REB (Ts = 10 ms).
 *
 * A dupla CAN desempacota os frames recebidos para reb_inputs_t
 * e empacota reb_outputs_t nos frames de saída.
 *
 * @param  ctx  Contexto global (persistente entre chamadas)
 * @param  in   Entradas decodificadas pela dupla CAN
 * @param  out  Saídas a serem codificadas pela dupla CAN
 */
void reb_step(reb_ctx_t *ctx,
              const reb_inputs_t *in,
              reb_outputs_t *out);

/**
 * @brief  Retorna o estado atual da FSM (thread-safe para IHM).
 */
reb_state_t reb_get_state(const reb_ctx_t *ctx);

/**
 * @brief  Retorna ponteiro somente-leitura para o log de eventos.
 */
const event_log_ctx_t *reb_get_log(const reb_ctx_t *ctx);

#endif /* REB_MAIN_H */
