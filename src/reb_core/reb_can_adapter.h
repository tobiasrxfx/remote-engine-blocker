#ifndef REB_CAN_ADAPTER_H
#define REB_CAN_ADAPTER_H

#include "reb_types.h"
#include "can_rx.h"
#include "can_tx.h"

void reb_can_adapter_rx_to_inputs(
    const can_rx_message_t *rx,
    RebInputs *inputs,
    uint32_t now_ms);

void reb_can_adapter_outputs_to_tx(
    const RebContext *context,
    const RebOutputs *outputs,
    can_tx_message_t *tx);

#endif