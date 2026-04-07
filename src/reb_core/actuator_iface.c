/**
 * @file    actuator_iface.c
 * @brief   REB — Implementação do Safety Floor de Combustível (FR-009)
 *
 * Esta função é a ÚNICA autorizada a produzir derate_pct_out.
 * Ela é chamada APÓS qualquer lógica upstream da FSM, garantindo
 * que o piso de 10% seja aplicado de forma independente de falhas
 * em outros módulos (NFR-SAF-001).
 *
 * Critério de aceitação FR-009:
 *   0 amostras com derate_pct_out < 10% quando
 *   vehicle_speed > V_STOP_KMH e state == STATE_BLOCKING.
 */

#include "actuator_iface.h"
#include "reb/reb_params.h"
#include <string.h>

/* Macro segura para MAX sem expansão dupla de argumentos */
#define U8_MAX2(a, b)  (((a) > (b)) ? (a) : (b))

void act_init(actuator_output_t *out)
{
    (void)memset(out, 0, sizeof(*out));
    out->derate_pct  = (uint8_t)DERATE_PCT_INIT; /* 100% — sem derating */
    out->starter_ok  = true;
}

void act_apply_derate(reb_state_t state,
                      float vehicle_speed,
                      uint8_t derate_pct_in,
                      actuator_output_t *out)
{
    bool in_blocking_motion;

    in_blocking_motion = (state == STATE_BLOCKING) &&
                         (vehicle_speed > V_STOP_KMH);

    if (in_blocking_motion) {
        /* FR-009: piso mínimo garantido independente de lógica upstream */
        out->derate_pct           = U8_MAX2(derate_pct_in, (uint8_t)FUEL_FLOOR_PCT);
        out->fuel_derating_active = true;
    } else if (state == STATE_BLOCKING) {
        /* Veículo parado em BLOCKING: derating ativo mas pode chegar a 0 */
        out->derate_pct           = derate_pct_in;
        out->fuel_derating_active = true;
    } else if (state == STATE_BLOCKED) {
        /* FR-011: em BLOCKED, derating é liberado (starter inhibit assume) */
        out->derate_pct           = (uint8_t)DERATE_PCT_INIT;
        out->fuel_derating_active = false;
    } else {
        /* IDLE ou THEFT_CONFIRMED: sem derating */
        out->derate_pct           = (uint8_t)DERATE_PCT_INIT;
        out->fuel_derating_active = false;
    }
}

void act_set_starter_inhibit(bool inhibit, actuator_output_t *out)
{
    out->starter_inhibit_active = inhibit;
    out->starter_ok             = !inhibit;
}
