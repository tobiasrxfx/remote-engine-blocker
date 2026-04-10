#include "reb_config.h"
#include "reb_persistence.h"
#include <stdio.h>
#include <stdint.h>

typedef struct
{
    RebContext context;
    uint32_t crc;
} RebPersistentData;

static uint32_t calculate_crc(const RebContext *ctx)
{
    const uint8_t *data = (const uint8_t *)ctx;
    uint32_t crc = 0;

    for (size_t i = 0; i < sizeof(RebContext); i++)
    {
        crc += data[i];
    }
    return crc;
}

bool reb_persistence_save(const RebContext *context)
{
    FILE *file = fopen("artifacts/reb_state.bin", "wb");
    if (!file) return false;

    RebPersistentData data;
    data.context = *context;
    data.crc = calculate_crc(context);

    fwrite(&data, sizeof(data), 1, file);
    fclose(file);
    return true;
}

bool reb_persistence_load(RebContext *context)
{
    FILE *file = fopen("artifacts/reb_state.bin", "rb");
    if (!file) return false;

    RebPersistentData data;
    fread(&data, sizeof(data), 1, file);
    fclose(file);

    if (data.crc != calculate_crc(&data.context))
        return false;

    *context = data.context;
    context->persisted_state_valid = true;
    return true;
}