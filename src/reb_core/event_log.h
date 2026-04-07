/**
 * @file    event_log.h
 * @brief   REB — Log de Eventos para Diagnóstico Forense (NFR-INFO-001)
 */

#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include "reb/reb_types.h"
#include "reb/reb_params.h"

/* Códigos de evento */
#define EVT_STATE_TRANSITION        0x01U
#define EVT_AUTH_FAIL               0x02U
#define EVT_AUTH_OK                 0x03U
#define EVT_PANEL_LOCKOUT           0x04U
#define EVT_SENSOR_THEFT            0x05U
#define EVT_DERATE_ACTIVE           0x06U
#define EVT_STARTER_INHIBIT         0x07U
#define EVT_UNBLOCK                 0x08U
#define EVT_REVERSAL_ABORT          0x09U
#define EVT_REVERSAL_EXPIRE         0x0AU
#define EVT_NVM_WRITE               0x0BU
#define EVT_NVM_RESTORE             0x0CU
#define EVT_SIGNAL_FAULT            0x0DU
#define EVT_CMD_RECEIVED            0x0EU
#define EVT_SPEED_SAFE_STOP         0x0FU

typedef struct {
    event_record_t entries[EVENT_LOG_MAX_ENTRIES];
    uint16_t       head;  /**< Próxima posição de escrita (circular) */
    uint16_t       count; /**< Total de entradas escritas            */
} event_log_ctx_t;

void evlog_init(event_log_ctx_t *ctx);
void evlog_write(event_log_ctx_t *ctx, uint32_t ts_ms,
                 uint8_t code, reb_state_t from, reb_state_t to,
                 uint8_t source, uint8_t auth_fail);
const event_record_t *evlog_get(const event_log_ctx_t *ctx, uint16_t idx);
uint16_t evlog_count(const event_log_ctx_t *ctx);

#endif /* EVENT_LOG_H */
