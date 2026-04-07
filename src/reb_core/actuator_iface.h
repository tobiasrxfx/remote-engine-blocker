/**
 * @file    actuator_iface.h
 * @brief   REB — Interface de Atuação com Safety Floor (FR-009, NFR-SAF-001)
 *
 * Camada independente da FSM que garante derate_pct >= 10% enquanto
 * o veículo está em movimento e blocking_state == BLOCKING.
 */

#ifndef ACTUATOR_IFACE_H
#define ACTUATOR_IFACE_H

#include "reb/reb_types.h"
#include <stdbool.h>
#include <stdint.h>

void act_init(actuator_output_t *out);

/**
 * @brief  Aplica piso de segurança de combustível (FR-009).
 *
 * derate_pct_out = MAX(derate_pct_in, FUEL_FLOOR_PCT)
 *   quando vehicle_speed > V_STOP_KMH e state == STATE_BLOCKING.
 *
 * @param  state          Estado atual da FSM
 * @param  vehicle_speed  Velocidade km/h
 * @param  derate_pct_in  Percentual calculado pela lógica upstream [0..100]
 * @param  out            Saída com valor corrigido
 */
void act_apply_derate(reb_state_t state,
                      float vehicle_speed,
                      uint8_t derate_pct_in,
                      actuator_output_t *out);

/**
 * @brief  Atualiza flag de inibição do starter (FR-011).
 * @param  inhibit  TRUE para bloquear partida
 * @param  out      Saída
 */
void act_set_starter_inhibit(bool inhibit, actuator_output_t *out);

#endif /* ACTUATOR_IFACE_H */
