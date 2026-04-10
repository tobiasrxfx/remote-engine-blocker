#include "reb_types.h"
#include "reb_config.h"

bool reb_rules_safe_to_block(const RebInputs *inputs)
{
    return (inputs->vehicle_speed_kmh <= REB_MAX_SPEED_FOR_BLOCK_KMH) &&
           (inputs->engine_rpm <= REB_ENGINE_RPM_LIMIT);
}