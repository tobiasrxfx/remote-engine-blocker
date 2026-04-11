#include "reb_can_adapter.h"
#include <string.h>

void reb_can_adapter_rx_to_inputs(
    const can_rx_message_t *rx,
    RebInputs *inputs,
    uint32_t now_ms)
{
    if ((rx == NULL) || (inputs == NULL))
        return;

    memset(inputs, 0, sizeof(*inputs));

    inputs->timestamp_ms = now_ms;

    switch (rx->msg_id)
    {
        case CAN_MSG_REB_CMD:
            inputs->remote_command =
                (RebRemoteCommand)rx->data.reb_cmd.cmd_type;

            inputs->nonce = rx->data.reb_cmd.cmd_nonce;
            break;

        case CAN_MSG_TCU_TO_REB:
            inputs->tcu_ack_received =
                (rx->data.tcu_to_reb.tcu_cmd == CAN_TCU_CMD_ACK);
            break;

        default:
            break;
    }
}

void reb_can_adapter_outputs_to_tx(
    const RebContext *context,
    const RebOutputs *outputs,
    can_tx_message_t *tx)
{
    if ((context == NULL) || (outputs == NULL) || (tx == NULL))
    {
        return;
    }

    tx->msg_id = CAN_MSG_REB_STATUS;

    switch (context->current_state)
    {
        case REB_STATE_IDLE:
            tx->data.reb_status.status_code = CAN_STATUS_IDLE;
            break;

        case REB_STATE_THEFT_CONFIRMED:
            tx->data.reb_status.status_code = CAN_STATUS_THEFT_CONFIRMED;
            break;

        case REB_STATE_BLOCKING:
            tx->data.reb_status.status_code = CAN_STATUS_BLOCKING;
            break;

        case REB_STATE_BLOCKED:
            tx->data.reb_status.status_code = CAN_STATUS_BLOCKED;
            break;

        default:
            tx->data.reb_status.status_code = CAN_STATUS_IDLE;
            break;
    }

    tx->data.reb_status.blocked_flag =
        outputs->starter_lock ? 1U : 0U;

    tx->data.reb_status.vehicle_speed_centi_kmh = 0U;
    tx->data.reb_status.error_code = 0U;
    tx->data.reb_status.reserved = 0U;
}
