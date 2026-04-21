#include "reb_can_adapter.h"
#include <string.h>

void reb_can_adapter_rx_to_inputs(
    const can_rx_message_t *rx,
    RebInputs *inputs,
    uint32_t now_ms)
{
    if ((rx == NULL) || (inputs == NULL))
        return;

    // memset(inputs, 0, sizeof(*inputs));

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

        case CAN_MSG_VEHICLE_STATE:
            inputs->vehicle_speed_kmh =
                rx->data.vehicle_state.vehicle_speed_centi_kmh / 100;

            inputs->engine_rpm =
                rx->data.vehicle_state.engine_rpm;

            break;

        case CAN_MSG_BCM_INTRUSION_STATUS:
            inputs->intrusion_detected =
                (rx->data.bcm_intrusion.intrusion_level != CAN_INTRUSION_NONE);
            break;

        case CAN_MSG_PANEL_AUTH_CMD:
            if (rx->data.panel_auth.auth_request)
            {
                inputs->remote_command = REB_REMOTE_UNLOCK;
                inputs->nonce = rx->data.panel_auth.auth_nonce;
                inputs->tcu_ack_received = true;
            }
            break;

        case CAN_MSG_PANEL_CANCEL_CMD:
            if (rx->data.panel_cancel.cancel_request)
            {
                inputs->remote_command = REB_REMOTE_CANCEL;
            }
            break;

        default:
            break;
    }
}

void reb_can_adapter_outputs_to_tx(
    const RebContext *context,
    const RebOutputs *outputs,
    can_tx_message_t *tx_list,
    uint32_t *tx_count)
{
    uint32_t idx = 0;

    if ((context == NULL) || (outputs == NULL) || (tx_list == NULL) || (tx_count == NULL))
    {
        return;
    }

    // =========================
    // 1. REB_STATUS (0x201)
    // =========================
    tx_list[idx].msg_id = CAN_MSG_REB_STATUS;

    switch (context->current_state)
    {
        case REB_STATE_IDLE:
            tx_list[idx].data.reb_status.status_code = CAN_STATUS_IDLE;
            break;

        case REB_STATE_THEFT_CONFIRMED:
            tx_list[idx].data.reb_status.status_code = CAN_STATUS_THEFT_CONFIRMED;
            break;

        case REB_STATE_BLOCKING:
            tx_list[idx].data.reb_status.status_code = CAN_STATUS_BLOCKING;
            break;

        case REB_STATE_BLOCKED:
            tx_list[idx].data.reb_status.status_code = CAN_STATUS_BLOCKED;
            break;

        default:
            tx_list[idx].data.reb_status.status_code = CAN_STATUS_IDLE;
            break;
    }

    tx_list[idx].data.reb_status.blocked_flag =
        outputs->starter_lock ? 1U : 0U;

    tx_list[idx].data.reb_status.vehicle_speed_centi_kmh = 0U;
    tx_list[idx].data.reb_status.error_code = 0U;
    tx_list[idx].data.reb_status.reserved = 0U;

    idx++;

    // =========================
    // 2. DERATE (0x400) - always send
    // =========================
    tx_list[idx].msg_id = CAN_MSG_REB_DERATE_CMD;
    tx_list[idx].data.reb_derate_cmd.derate_pct = outputs->derate_percent;

    tx_list[idx].data.reb_derate_cmd.derate_mode =
        (outputs->derate_percent > 0U)
            ? CAN_DERATE_MODE_GRADUAL_RAMP
            : CAN_DERATE_MODE_OFF;

    tx_list[idx].data.reb_derate_cmd.safety_flag =
        (outputs->derate_percent > 0U) ? 1U : 0U;

    idx++;

    // =========================
    // 3. PREVENT START (0x401) - always send
    // =========================
    tx_list[idx].msg_id = CAN_MSG_REB_PREVENT_START;

    tx_list[idx].data.reb_prevent_start.prevent_start =
        outputs->starter_lock ? CAN_PREVENT_START_BLOCK
                              : CAN_PREVENT_START_ALLOW;

    tx_list[idx].data.reb_prevent_start.auth_token_lsb =
        (uint8_t)(context->last_valid_nonce & 0xFFU);

    idx++;

    *tx_count = idx;
}
