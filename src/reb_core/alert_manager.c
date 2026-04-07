/**
 * @file    alert_manager.c
 * @brief   REB — Implementação dos Alertas (FR-013)
 *
 * Buzina oscila a 1Hz: 50 ciclos ON, 50 ciclos OFF (Ts=10ms).
 * Critério FR-013: horn_cmd e hazard ativados em < 100ms após timer.
 */

#include "alert_manager.h"
#include "reb/reb_params.h"
#include <string.h>

/* 1 Hz = período de 1000ms → 100 ciclos (Ts=10ms) → 50 ciclos por semiciclo */
#define HORN_HALF_PERIOD_CYCLES  50U

void alert_mgr_init(alert_ctx_t *ctx)
{
    (void)memset(ctx, 0, sizeof(*ctx));
}

void alert_mgr_start(alert_ctx_t *ctx)
{
    ctx->alerts_active = true;
    ctx->horn_timer    = 0U;
    ctx->horn_state    = true; /* inicia ligada */
}

void alert_mgr_step(alert_ctx_t *ctx, alert_output_t *out)
{
    if (!ctx->alerts_active) {
        out->horn_active   = false;
        out->hazard_active = false;
        out->hmi_alert     = false;
        return;
    }

    /* Oscilação da buzina a 1Hz */
    ctx->horn_timer++;
    if (ctx->horn_timer >= (uint32_t)HORN_HALF_PERIOD_CYCLES) {
        ctx->horn_timer = 0U;
        ctx->horn_state = !ctx->horn_state;
    }

    out->horn_active   = ctx->horn_state;
    out->hazard_active = true;  /* Pisca-alerta contínuo */
    out->hmi_alert     = true;  /* Alerta HMI contínuo   */
}

void alert_mgr_stop(alert_ctx_t *ctx, alert_output_t *out)
{
    ctx->alerts_active = false;
    ctx->horn_timer    = 0U;
    ctx->horn_state    = false;

    out->horn_active   = false;
    out->hazard_active = false;
    out->hmi_alert     = false;
}
