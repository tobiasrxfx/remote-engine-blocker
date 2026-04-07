/**
 * @file    alert_manager.h
 * @brief   REB — Gerenciador de Alertas Visuais e Sonoros (FR-013)
 *
 * Ativa buzina (1Hz intermitente), pisca-alerta e alerta HMI
 * após expiração do timer de segurança de FR-012.
 */

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include "reb/reb_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     alerts_active;    /**< Alertas em andamento                  */
    uint32_t horn_timer;       /**< Ciclos para oscilação da buzina 1Hz   */
    bool     horn_state;       /**< Estado atual da buzina (ON/OFF)       */
} alert_ctx_t;

void alert_mgr_init(alert_ctx_t *ctx);

/**
 * @brief  Ativa os alertas multimodais (FR-013).
 */
void alert_mgr_start(alert_ctx_t *ctx);

/**
 * @brief  Executa um ciclo — atualiza oscilação da buzina.
 * @param  ctx   Contexto
 * @param  out   Saída de alertas
 */
void alert_mgr_step(alert_ctx_t *ctx, alert_output_t *out);

/**
 * @brief  Desativa todos os alertas (desbloqueio ou senha válida).
 */
void alert_mgr_stop(alert_ctx_t *ctx, alert_output_t *out);

#endif /* ALERT_MANAGER_H */
