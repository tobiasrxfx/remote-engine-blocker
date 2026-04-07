/**
 * @file    fsm.h
 * @brief   REB — Máquina de Estados Principal (FR-001, FR-002, FR-003)
 *
 * Migração direta do Stateflow (EngineblockerController).
 * Cada estado possui entry/during/exit separados (MISRA C §14.4).
 *
 * Diagrama de estados (SRS §6.1):
 *   IDLE → THEFT_CONFIRMED → BLOCKING → BLOCKED
 *   BLOCKED → IDLE (via comando autenticado de desbloqueio)
 */

#ifndef FSM_H
#define FSM_H

#include "reb/reb_types.h"
#include "security_manager.h"
#include "panel_auth.h"
#include "sensor_fusion.h"
#include "actuator_iface.h"
#include "starter_control.h"
#include "alert_manager.h"
#include "reversal_window.h"
#include "nvm.h"
#include "event_log.h"
#include <stdbool.h>
#include <stdint.h>

/* Contexto completo da FSM — agrega todos os sub-módulos */
typedef struct {
    /* Estado principal */
    fsm_ctx_t          fsm;

    /* Sub-módulos */
    sec_ctx_t          sec;
    panel_auth_ctx_t   panel;
    sf_ctx_t           sf;
    actuator_output_t  act;
    starter_ctx_t      starter;
    alert_ctx_t        alert;
    rw_ctx_t           rw;
    event_log_ctx_t    log;

    /* Cálculo de derate progressivo */
    float              derate_ramp;   /**< Valor atual da rampa [0..100]   */

    /* Controle interno de status CAN */
    uint32_t           status_timer;  /**< Ciclos para próximo REB_STATUS  */
} reb_ctx_t;

/**
 * @brief  Inicializa a FSM e tenta restaurar estado do NVM (NFR-REL-001).
 */
void reb_fsm_init(reb_ctx_t *ctx);

/**
 * @brief  Executa um ciclo da FSM (Ts = 10ms).
 * @param  ctx  Contexto completo
 * @param  in   Entradas deste ciclo
 * @param  out  Saídas calculadas
 */
void reb_fsm_step(reb_ctx_t *ctx,
                  const reb_inputs_t *in,
                  reb_outputs_t *out);

#endif /* FSM_H */
