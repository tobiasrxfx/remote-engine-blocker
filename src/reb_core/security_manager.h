/**
 * @file    security_manager.h
 * @brief   REB — Gerenciador de Segurança Anti-Replay (NFR-SEC-001)
 *
 * Implementa: nonce monotônico, validação de timestamp,
 * verificação de assinatura (stub HMAC para MIL).
 */

#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include "reb/reb_types.h"
#include "event_log.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  Verifica um frame REB_CMD completo.
 * @param  ctx         Contexto do security manager
 * @param  nonce       Nonce recebido no frame 0x200
 * @param  timestamp_ms Timestamp em ms recebido no frame 0x200
 * @param  sig_ok      Flag de assinatura válida (simulada em MIL)
 * @param  current_ms  Tempo atual da simulação em ms
 * @param  result      Saída: AUTH_OK ou código de falha
 * @return true se autenticado, false caso contrário
 */
bool sec_mgr_verify(sec_ctx_t *ctx,
                    uint16_t nonce,
                    uint32_t timestamp_ms,
                    bool sig_ok,
                    uint32_t current_ms,
                    auth_fail_t *result);

void sec_mgr_init(sec_ctx_t *ctx);

#endif /* SECURITY_MANAGER_H */
