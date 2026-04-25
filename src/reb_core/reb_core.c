#include "reb_core.h"
#include "reb_persistence.h"
#include "reb_state_machine.h"

void reb_core_init(RebContext *context)
{
    if (!reb_persistence_load(context))
    {
        reb_state_machine_init(context);
    }
}

void reb_core_execute(RebContext *context,
                      const RebInputs *inputs,
                      RebOutputs *outputs)
{
    reb_state_machine_step(context, inputs, outputs);
    (void)reb_persistence_save(context);
}