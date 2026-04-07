/**
 * @file    nvm.c
 * @brief   REB — NVM Simulada com CRC32 (NFR-REL-001)
 *
 * Em MIL: dados persistidos em variável estática.
 * CRC32 simples (polinômio 0xEDB88320) sem lookup table para MISRA C.
 *
 * Para target real: substituir nvm_sim_buf por leitura/escrita
 * em flash/EEPROM via HAL.
 */

#include "nvm.h"
#include <string.h>

/* -------------------------------------------------------------------------
 * HAL simulada — substituir por driver real em target
 * ------------------------------------------------------------------------- */
static nvm_data_t nvm_sim_buf;
static bool       nvm_sim_valid = false;

/* CRC32 (IEEE 802.3) sem tabela — adequado para MISRA C */
static uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint8_t  bit;

    for (i = 0U; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

nvm_result_t nvm_write_state(const nvm_data_t *data)
{
    nvm_data_t buf;
    (void)memcpy(&buf, data, sizeof(buf));

    /* Calcula CRC sobre todos os campos exceto o próprio crc32 */
    buf.crc32 = crc32_compute((const uint8_t *)&buf,
                               (uint32_t)(sizeof(buf) - sizeof(buf.crc32)));

    /* Escreve na "flash" simulada */
    (void)memcpy(&nvm_sim_buf, &buf, sizeof(nvm_sim_buf));
    nvm_sim_valid = true;

    return NVM_OK;
}

nvm_result_t nvm_read_state(nvm_data_t *data)
{
    uint32_t expected_crc;

    if (!nvm_sim_valid) {
        return NVM_ERR_EMPTY;
    }

    (void)memcpy(data, &nvm_sim_buf, sizeof(*data));

    /* Verifica CRC */
    expected_crc = crc32_compute((const uint8_t *)data,
                                  (uint32_t)(sizeof(*data) - sizeof(data->crc32)));
    if (expected_crc != data->crc32) {
        return NVM_ERR_CRC;
    }

    return NVM_OK;
}

void nvm_invalidate(void)
{
    (void)memset(&nvm_sim_buf, 0, sizeof(nvm_sim_buf));
    nvm_sim_valid = false;
}

bool nvm_is_valid(void)
{
    return nvm_sim_valid;
}
