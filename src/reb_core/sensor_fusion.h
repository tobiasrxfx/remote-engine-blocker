/**
 * @file    sensor_fusion.h
 * @brief   REB — Fusão de Sensores para Detecção Automática (FR-007)
 */

#ifndef SENSOR_FUSION_H
#define SENSOR_FUSION_H

#include "reb/reb_types.h"
#include <stdbool.h>
#include <stdint.h>

void sf_init(sf_ctx_t *ctx);

/**
 * @brief  Executa um ciclo de fusão de sensores.
 * @param  ctx          Contexto
 * @param  accel_peak   Aceleração normalizada [0..1] (DBC fator 0.01)
 * @param  glass_break  Sinal de quebra de vidro [0..1]
 * @param  out          Saídas calculadas
 */
void sf_step(sf_ctx_t *ctx,
             float accel_peak,
             float glass_break,
             sf_output_t *out);

#endif /* SENSOR_FUSION_H */
