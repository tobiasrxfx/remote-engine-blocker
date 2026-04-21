#include <winsock2.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "can_frame.h"
#include "can_monitor.h"
#include "can_rx.h"
#include "can_socket_transport.h"
#include "can_tx.h"
#include "can_codec.h"

#include "reb_core.h"
#include "reb_can_adapter.h"
#include "reb_types.h"
#include "reb_state_machine.h"

#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT (5001U)

/**
 * @brief Convert REB state to string for debug prints.
 */
static const char *reb_state_to_string(RebState state)
{
    switch (state)
    {
        case REB_STATE_IDLE:
            return "IDLE";
        case REB_STATE_THEFT_CONFIRMED:
            return "THEFT_CONFIRMED";
        case REB_STATE_BLOCKING:
            return "BLOCKING";
        case REB_STATE_BLOCKED:
            return "BLOCKED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Print decoded RX information for debugging.
 */
static void print_rx_message(const can_rx_message_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    switch (msg->msg_id)
    {
        case CAN_MSG_REB_CMD:
            printf("Received REB_CMD\n");
            printf("  cmd_type      = %u\n",
                   (unsigned int)msg->data.reb_cmd.cmd_type);
            printf("  cmd_mode      = %u\n",
                   (unsigned int)msg->data.reb_cmd.cmd_mode);
            printf("  cmd_nonce     = 0x%04X\n",
                   (unsigned int)msg->data.reb_cmd.cmd_nonce);
            printf("  cmd_timestamp = 0x%08X\n",
                   (unsigned int)msg->data.reb_cmd.cmd_timestamp);
            break;

        case CAN_MSG_TCU_TO_REB:
            printf("Received TCU_TO_REB\n");
            printf("  tcu_cmd        = %u\n",
                   (unsigned int)msg->data.tcu_to_reb.tcu_cmd);
            printf("  fail_reason    = %u\n",
                   (unsigned int)msg->data.tcu_to_reb.fail_reason);
            printf("  echo_timestamp = 0x%08X\n",
                   (unsigned int)msg->data.tcu_to_reb.echo_timestamp);
            break;

        case CAN_MSG_VEHICLE_STATE:
            printf("Received VEHICLE_STATE\n");
            printf("  vehicle_speed  = %u (centi-km/h)\n",
                   (unsigned int)msg->data.vehicle_state.vehicle_speed_centi_kmh);
            printf("  ignition_on    = %u\n",
                   (unsigned int)msg->data.vehicle_state.ignition_on);
            printf("  engine_running = %u\n",
                   (unsigned int)msg->data.vehicle_state.engine_running);
            printf("  engine_rpm     = %u\n",
                   (unsigned int)msg->data.vehicle_state.engine_rpm);
            break;

        case CAN_MSG_BCM_INTRUSION_STATUS:
            printf("Received BCM_INTRUSION_STATUS\n");
            printf("  door_open      = %u\n",
                   (unsigned int)msg->data.bcm_intrusion.door_open);
            printf("  glass_break    = %u\n",
                   (unsigned int)msg->data.bcm_intrusion.glass_break);
            printf("  shock_detected = %u\n",
                   (unsigned int)msg->data.bcm_intrusion.shock_detected);
            printf("  intrusion_lvl  = %u\n",
                   (unsigned int)msg->data.bcm_intrusion.intrusion_level);
            break;

        case CAN_MSG_PANEL_AUTH_CMD:
            printf("Received PANEL_AUTH_CMD\n");
            printf("  auth_request   = %u\n",
                   (unsigned int)msg->data.panel_auth.auth_request);
            printf("  auth_method    = %u\n",
                   (unsigned int)msg->data.panel_auth.auth_method);
            printf("  auth_nonce     = 0x%04X\n",
                   (unsigned int)msg->data.panel_auth.auth_nonce);
            break;

        case CAN_MSG_PANEL_CANCEL_CMD:
            printf("Received PANEL_CANCEL_CMD\n");
            printf("  cancel_request = %u\n",
                   (unsigned int)msg->data.panel_cancel.cancel_request);
            printf("  cancel_reason  = %u\n",
                   (unsigned int)msg->data.panel_cancel.cancel_reason);
            printf("  cancel_nonce   = 0x%04X\n",
                   (unsigned int)msg->data.panel_cancel.cancel_nonce);
            break;

        default:
            printf("Received unsupported decoded message\n");
            break;
    }
}

/**
 * @brief Print REB context for debugging.
 */
static void print_reb_context(const RebContext *context)
{
    if (context == NULL)
    {
        return;
    }

    printf("REB context\n");
    printf("  current_state        = %s\n",
           reb_state_to_string(context->current_state));
    printf("  last_valid_nonce     = %u\n",
           (unsigned int)context->last_valid_nonce);
    printf("  invalid_unlocks      = %u\n",
           (unsigned int)context->invalid_unlock_attempts);
}

/**
 * @brief Print generated outputs for debugging.
 */
static void print_reb_outputs(const RebOutputs *outputs)
{
    if (outputs == NULL)
    {
        return;
    }

    printf("REB outputs\n");
    printf("  visual_alert       = %u\n", (unsigned int)outputs->visual_alert);
    printf("  acoustic_alert     = %u\n", (unsigned int)outputs->acoustic_alert);
    printf("  starter_lock       = %u\n", (unsigned int)outputs->starter_lock);
    printf("  derate_percent     = %u\n", (unsigned int)outputs->derate_percent);
    printf("  send_status_to_tcu = %u\n", (unsigned int)outputs->send_status_to_tcu);
}

/**
 * @brief Print TX frame for debugging.
 */
static void print_tx_frame(const can_frame_t *frame)
{
    uint8_t i;

    if (frame == NULL)
    {
        return;
    }

    printf("Sending CAN frame\n");
    printf("  ID   = 0x%03X\n", (unsigned int)frame->id);
    printf("  DLC  = %u\n", (unsigned int)frame->dlc);
    printf("  DATA =");

    for (i = 0U; i < CAN_FRAME_MAX_DATA_LEN; i++)
    {
        printf(" %02X", frame->data[i]);
    }

    printf("\n");
}

/**
 * @brief Check if the received message is being used as a time tick.
 *
 * Convention used for tests:
 * - CAN_MSG_TCU_TO_REB
 * - tcu_cmd == CAN_TCU_CMD_RETRY
 * - echo_timestamp contains the new absolute time in ms
 */
static bool is_time_tick_message(const can_rx_message_t *rx_msg)
{
    if (rx_msg == NULL)
    {
        return false;
    }

    return (rx_msg->msg_id == CAN_MSG_TCU_TO_REB) &&
           (rx_msg->data.tcu_to_reb.tcu_cmd == CAN_TCU_CMD_RETRY);
}

/**
 * @brief Extract next time from a time tick message.
 */
static uint32_t extract_next_time_ms(
    const can_rx_message_t *rx_msg,
    uint32_t current_now_ms)
{
    if (is_time_tick_message(rx_msg))
    {
        return rx_msg->data.tcu_to_reb.echo_timestamp;
    }

    return current_now_ms;
}

/**
 * @brief Clear transient inputs after each execution cycle.
 *
 * Persistent signals like speed / ignition / rpm remain latched until updated.
 * Event-like inputs are consumed once.
 */
static void clear_transient_inputs(RebInputs *inputs)
{
    if (inputs == NULL)
    {
        return;
    }

    inputs->remote_command = REB_REMOTE_NONE;
    inputs->tcu_ack_received = false;
    inputs->intrusion_detected = false;
    inputs->nonce = 0U;
}

/**
 * @brief Parse startup mode from command line arguments.
 *
 * Supported:
 *   --fresh-start
 */
static bool is_fresh_start_requested(int argc, char *argv[])
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if ((argv[i] != NULL) &&
            (strcmp(argv[i], "--fresh-start") == 0))
        {
            return true;
        }
    }

    return false;
}

int main(int argc, char *argv[])
{
    WSADATA wsa;
    SOCKET server_socket;
    SOCKET client_socket;
    struct sockaddr_in server_addr;

    can_frame_t rx_frame;
    can_frame_t tx_frame;
    can_rx_message_t rx_msg;
    can_tx_message_t tx_msg;
    can_monitor_t monitor;
    can_socket_transport_t transport;

    RebContext context;
    RebInputs inputs;
    RebOutputs outputs;

    can_socket_transport_status_t transport_status;
    can_rx_status_t rx_status;
    can_tx_status_t tx_status;

    uint32_t now_ms = 1000U;
    int exit_code = 0;
    bool fresh_start;

    fresh_start = is_fresh_start_requested(argc, argv);

    printf("REB loop server starting...\n");
    printf("Startup mode: %s\n", fresh_start ? "FRESH_START" : "NORMAL_PERSISTENT");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("WSAStartup failed\n");
        return 1;
    }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET)
    {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((u_short)SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        printf("Bind failed\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, 1) != 0)
    {
        printf("Listen failed\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Waiting for connection on port %u...\n", (unsigned int)SERVER_PORT);

    client_socket = accept(server_socket, NULL, NULL);
    if (client_socket == INVALID_SOCKET)
    {
        printf("Accept failed\n");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("Client connected\n");

    can_socket_transport_init(&transport);
    transport_status = can_socket_transport_startup(&transport);
    if (transport_status != CAN_SOCKET_TRANSPORT_STATUS_OK)
    {
        printf("Transport startup failed: %d\n", transport_status);
        closesocket(client_socket);
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    transport_status = can_socket_transport_attach_socket(&transport, client_socket);
    if (transport_status != CAN_SOCKET_TRANSPORT_STATUS_OK)
    {
        printf("Attach socket failed: %d\n", transport_status);
        (void)can_socket_transport_shutdown(&transport);
        closesocket(client_socket);
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    can_monitor_init(&monitor);
    (void)memset(&inputs, 0, sizeof(inputs));
    (void)memset(&outputs, 0, sizeof(outputs));

    if (fresh_start)
    {
        reb_state_machine_init(&context);
        printf("Context initialized from fresh start (persistence ignored)\n");
    }
    else
    {
        reb_core_init(&context);
        printf("Context initialized through reb_core_init (persistence allowed)\n");
    }

    while (true)
    {
        transport_status = can_socket_transport_receive_frame(&transport, &rx_frame);
        if (transport_status == CAN_SOCKET_TRANSPORT_STATUS_CONNECTION_CLOSED)
        {
            printf("Client disconnected\n");
            break;
        }

        if (transport_status != CAN_SOCKET_TRANSPORT_STATUS_OK)
        {
            printf("Receive/deserialize failed: %d\n", transport_status);
            exit_code = 1;
            break;
        }

        rx_status = can_rx_process_frame(&rx_frame, &rx_msg);
        if (rx_status != CAN_RX_STATUS_OK)
        {
            printf("RX processing failed: %d\n", rx_status);
            exit_code = 1;
            break;
        }

        print_rx_message(&rx_msg);
        (void)can_monitor_on_rx(&monitor, rx_msg.msg_id, now_ms);

        if (is_time_tick_message(&rx_msg))
        {
            now_ms = extract_next_time_ms(&rx_msg, now_ms);
            inputs.timestamp_ms = now_ms;
            printf("Time advanced to %u ms\n", (unsigned int)now_ms);
        }
        else
        {
            reb_can_adapter_rx_to_inputs(&rx_msg, &inputs, now_ms);
            inputs.timestamp_ms = now_ms;
        }

        reb_core_execute(&context, &inputs, &outputs);
        clear_transient_inputs(&inputs);

        print_reb_context(&context);
        print_reb_outputs(&outputs);

        can_tx_message_t tx_list[4];
        uint32_t tx_count = 0U;
        uint32_t i;

        reb_can_adapter_outputs_to_tx(&context, &outputs, tx_list, &tx_count);

        for (i = 0U; i < tx_count; i++)
        {
            can_frame_t tx_frame;

            tx_status = can_tx_build_frame(&tx_list[i], &tx_frame);
            if (tx_status != CAN_TX_STATUS_OK)
            {
                printf("TX build failed for msg_id %u: %d\n",
                    (unsigned int)tx_list[i].msg_id,
                    (int)tx_status);
                exit_code = 1;
                break;
            }

            (void)can_monitor_on_tx(&monitor, tx_list[i].msg_id, now_ms);
            print_tx_frame(&tx_frame);

            transport_status = can_socket_transport_send_frame(&transport, &tx_frame);
            if (transport_status != CAN_SOCKET_TRANSPORT_STATUS_OK)
            {
                printf("Send/serialize failed for msg_id %u: %d\n",
                    (unsigned int)tx_list[i].msg_id,
                    (int)transport_status);
                exit_code = 1;
                break;
            }
        }

        if (exit_code != 0)
        {
            break;
        }
    }

    can_socket_transport_detach_socket(&transport);
    (void)can_socket_transport_shutdown(&transport);

    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();

    printf("REB loop server finished\n");
    return exit_code;
}