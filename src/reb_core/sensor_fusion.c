/**
 * @file    sensor_fusion.c
 * @brief   REB — Implementação da Fusão de Sensores (FR-007)
 *
 * score = w_glass * glass_break + w_accel * (accel_peak / accel_max)
 * Debounce de 200 ciclos (2s) antes de ativar detecção.
 * Histerese: desativa quando score < SF_THRESH_HYST_LOW.
 */

#include "sensor_fusion.h"
#include "reb/reb_params.h"
#include <string.h>

void sf_init(sf_ctx_t *ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

void sf_step(sf_ctx_t *ctx,
             float accel_peak,
             float glass_break,
             sf_output_t *out)
{
    float accel_norm;
    float score;

    /* Normaliza aceleração [0..1] */
    accel_norm = clampf(accel_peak / ACCEL_MAX, 0.0f, 1.0f);

    /* Score ponderado */
    score = (SF_W_GLASS * clampf(glass_break, 0.0f, 1.0f)) +
            (SF_W_ACCEL * accel_norm);
    score = clampf(score, 0.0f, 1.0f);

    ctx->last_score = score;

    /* Lógica de ativação com debounce */
    if (!ctx->active) {
        if (score >= SF_THRESH) {
            ctx->debounce_cnt++;
            if (ctx->debounce_cnt >= (uint16_t)SF_DEBOUNCE_CYCLES) {
                ctx->active       = true;
                ctx->debounce_cnt = (uint16_t)SF_DEBOUNCE_CYCLES;
            }
        } else {
            ctx->debounce_cnt = 0U;
        }
    } else {
        /* Histerese de desativação (SRS L4 — open item, implementado aqui) */
        if (score < SF_THRESH_HYST_LOW) {
            ctx->active       = false;
            ctx->debounce_cnt = 0U;
        }
    }

    out->theft_score    = score;
    out->theft_detected = ctx->active;
    out->debounce_cnt   = ctx->debounce_cnt;
}
