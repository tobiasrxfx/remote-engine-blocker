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
    bool ret_val = false;
    FILE *file = fopen("artifacts/reb_state.bin", "wb");
    if (file != NULL)
    {
        RebPersistentData data;
        data.context = *context;
        data.crc = calculate_crc(context);

        (void)fwrite(&data, sizeof(data), 1, file);
        (void)fclose(file);

        ret_val = true;
    }
    else
    {
        /* No action required. Misra C:2012 Rule 15.7 */
    }
    
    return ret_val;
}

bool reb_persistence_load(RebContext *context)
{
    bool ret_val = false;
    FILE *file = fopen("artifacts/reb_state.bin", "rb");
    if (file != NULL) 
    {
        RebPersistentData data;
        (void)fread(&data, sizeof(data), 1, file);
        (void)fclose(file);

        if (data.crc == calculate_crc(&data.context))
        {
            *context = data.context;
            context->persisted_state_valid = true;
            ret_val = true;
        }
        else
        {
            /* No action required. Misra C:2012 Rule 15.7 */
        }

    }
    else
    {
        /* No action required. Misra C:2012 Rule 15.7 */
    }

    return ret_val;
}