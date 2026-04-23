/**
 * @file reb_can_adapter.c
 * @brief CAN Message adapter for the Remote Engine Blocker (REB)
 * Handles decoding of incoming CAN frames into system inputs.
 */

#include "reb_can_adapter.h"
#include <string.h>

/* CAN Message ID Definitions */
#define CAN_MSG_TCU_CMD 0x200
#define CAN_MSG_VEHICLE_STATE 0x500
#define CAN_MSG_BCM_INTRUSION 0x501
#define CAN_MSG_PANEL_UNLOCK_CMD 0x502
#define CAN_MSG_PANEL_CANCEL_CMD 0x503
#define CAN_MSG_PANEL_BLOCK_CMD 0x504 // Added for Manual Block (FR-004)

void reb_can_adapter_decode(const can_frame_t *rx, reb_inputs_t *inputs)
{
    if (rx == NULL || inputs == NULL)
        return;

    /* Reset pulse commands to avoid sticky states */
    inputs->remote_command = REB_REMOTE_NONE;
    inputs->tcu_ack_received = false;

    switch (rx->id)
    {
    case CAN_MSG_TCU_CMD:
        /* Remote Command from Smartphone/TCU */
        if (rx->data.tcu_cmd.payload == 0x01)
        {
            inputs->remote_command = REB_REMOTE_BLOCK;
        }
        else if (rx->data.tcu_cmd.payload == 0x02)
        {
            inputs->remote_command = REB_REMOTE_UNLOCK;
        }
        break;

    case CAN_MSG_VEHICLE_STATE:
        /* Vehicle Telemetry */
        inputs->vehicle_speed = rx->data.veh_state.speed;
        inputs->engine_rpm = rx->data.veh_state.rpm;
        inputs->ignition_on = rx->data.veh_state.ignition_on;
        inputs->engine_running = rx->data.veh_state.engine_running;
        break;

    case CAN_MSG_BCM_INTRUSION:
        /* Automatic Detection (FR-007) */
        if (rx->data.bcm_state.intrusion_detected)
        {
            inputs->remote_command = REB_REMOTE_BLOCK;
        }
        break;
    case CAN_MSG_PANEL_AUTH_CMD: // 0x502
        if (rx->data.panel_auth.auth_request)
        {
            // auth_method == 2 aciona o Bloqueio, caso contrário aciona o Desbloqueio
            if (rx->data.panel_auth.auth_method == 2)
            {
                inputs->remote_command = REB_REMOTE_BLOCK;
            }
            else
            {
                inputs->remote_command = REB_REMOTE_UNLOCK;
            }
            inputs->nonce = rx->data.panel_auth.auth_nonce;
            inputs->tcu_ack_received = true;
        }
        break;

    case CAN_MSG_PANEL_BLOCK_CMD:
        /* Local Manual Block from Dashboard (FR-004) */
        if (rx->data.panel_auth.auth_request)
        {
            inputs->remote_command = REB_REMOTE_BLOCK;
            inputs->nonce = rx->data.panel_auth.auth_nonce;
            inputs->tcu_ack_received = true;
        }
        break;

    case CAN_MSG_PANEL_UNLOCK_CMD:
        /* Local Manual Unlock from Dashboard (Section 6.2) */
        if (rx->data.panel_auth.auth_request)
        {
            inputs->remote_command = REB_REMOTE_UNLOCK;
            inputs->nonce = rx->data.panel_auth.auth_nonce;
            inputs->tcu_ack_received = true;
        }
        break;

    case CAN_MSG_PANEL_CANCEL_CMD:
        /* Local Abort/Cancel during Reversal Window (FR-012) */
        if (rx->data.panel_auth.auth_request)
        {
            inputs->remote_command = REB_REMOTE_CANCEL;
            inputs->nonce = rx->data.panel_auth.auth_nonce;
            inputs->tcu_ack_received = true;
        }
        break;

    default:
        /* Unknown Message ID */
        break;
    }
}
