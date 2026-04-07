/**
 * @file    nvm.h
 * @brief   REB — Persistência NVM (NFR-REL-001)
 *
 * Protege contra bypass por desconexão de bateria.
 * Em MIL: usa memória estática simulada.
 * Em target: substituir nvm_hal_read/write por drivers de flash/EEPROM.
 */

#ifndef NVM_H
#define NVM_H

#include "reb/reb_types.h"
#include <stdbool.h>

/** Retorno das operações NVM */
typedef enum {
    NVM_OK         = 0U,
    NVM_ERR_CRC    = 1U, /**< CRC inválido — dados corrompidos      */
    NVM_ERR_EMPTY  = 2U, /**< Memória virgem                        */
    NVM_ERR_WRITE  = 3U  /**< Falha de escrita                      */
} nvm_result_t;

nvm_result_t nvm_write_state(const nvm_data_t *data);
nvm_result_t nvm_read_state(nvm_data_t *data);
void         nvm_invalidate(void);
bool         nvm_is_valid(void);

#endif /* NVM_H */
