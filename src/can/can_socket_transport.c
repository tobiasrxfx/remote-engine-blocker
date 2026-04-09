#include "can_socket_transport.h"

#include <stddef.h>
#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/types.h>
#endif

/**
 * @brief Byte offsets used by the fixed wire format.
 */
#define CAN_SOCKET_TRANSPORT_ID_OFFSET           (0U)
#define CAN_SOCKET_TRANSPORT_ID_TYPE_OFFSET      (4U)
#define CAN_SOCKET_TRANSPORT_FRAME_TYPE_OFFSET   (5U)
#define CAN_SOCKET_TRANSPORT_DLC_OFFSET          (6U)
#define CAN_SOCKET_TRANSPORT_VERSION_OFFSET      (7U)
#define CAN_SOCKET_TRANSPORT_DATA_OFFSET         (8U)

/**
 * @brief Check whether a socket handle is valid.
 */
static bool can_socket_transport_is_socket_valid(
    can_socket_transport_socket_t socket_handle)
{
    return (socket_handle != CAN_SOCKET_TRANSPORT_INVALID_SOCKET);
}

/**
 * @brief Write an unsigned 32-bit value in big-endian format.
 */
static void can_socket_transport_write_u32_be(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)((value >> 24U) & 0xFFU);
    buffer[1] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[2] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[3] = (uint8_t)(value & 0xFFU);
}

/**
 * @brief Read an unsigned 32-bit value in big-endian format.
 */
static uint32_t can_socket_transport_read_u32_be(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0] << 24U) |
           ((uint32_t)buffer[1] << 16U) |
           ((uint32_t)buffer[2] << 8U) |
           (uint32_t)buffer[3];
}

/**
 * @brief Send the full frame buffer through the attached socket.
 */
static can_socket_transport_status_t can_socket_transport_send_all(
    can_socket_transport_socket_t socket_handle,
    const uint8_t *buffer,
    size_t buffer_size)
{
    size_t total_sent = 0U;

    while (total_sent < buffer_size)
    {
        const int bytes_remaining = (int)(buffer_size - total_sent);
        const int bytes_sent = send(
            socket_handle,
            (const char *)&buffer[total_sent],
            bytes_remaining,
            0);

        if (bytes_sent == 0)
        {
            return CAN_SOCKET_TRANSPORT_STATUS_CONNECTION_CLOSED;
        }

        if (bytes_sent < 0)
        {
            return CAN_SOCKET_TRANSPORT_STATUS_SOCKET_ERROR;
        }

        total_sent += (size_t)bytes_sent;
    }

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

/**
 * @brief Receive the full frame buffer from the attached socket.
 */
static can_socket_transport_status_t can_socket_transport_receive_all(
    can_socket_transport_socket_t socket_handle,
    uint8_t *buffer,
    size_t buffer_size)
{
    size_t total_received = 0U;

    while (total_received < buffer_size)
    {
        const int bytes_remaining = (int)(buffer_size - total_received);
        const int bytes_received = recv(
            socket_handle,
            (char *)&buffer[total_received],
            bytes_remaining,
            0);

        if (bytes_received == 0)
        {
            return CAN_SOCKET_TRANSPORT_STATUS_CONNECTION_CLOSED;
        }

        if (bytes_received < 0)
        {
            return CAN_SOCKET_TRANSPORT_STATUS_SOCKET_ERROR;
        }

        total_received += (size_t)bytes_received;
    }

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

void can_socket_transport_init(can_socket_transport_t *transport)
{
    if (transport != NULL)
    {
        transport->network_initialized = false;
        transport->socket_attached = false;
        transport->socket_handle = CAN_SOCKET_TRANSPORT_INVALID_SOCKET;
    }
}

can_socket_transport_status_t can_socket_transport_startup(
    can_socket_transport_t *transport)
{
    if (transport == NULL)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (transport->network_initialized)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_OK;
    }

#ifdef _WIN32
    {
        WSADATA wsa_data;
        const int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);

        if (result != 0)
        {
            return CAN_SOCKET_TRANSPORT_STATUS_SOCKET_ERROR;
        }
    }
#endif

    transport->network_initialized = true;

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

can_socket_transport_status_t can_socket_transport_shutdown(
    can_socket_transport_t *transport)
{
    if (transport == NULL)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (!transport->network_initialized)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_OK;
    }

#ifdef _WIN32
    if (WSACleanup() != 0)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_SOCKET_ERROR;
    }
#endif

    transport->network_initialized = false;

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

can_socket_transport_status_t can_socket_transport_attach_socket(
    can_socket_transport_t *transport,
    can_socket_transport_socket_t socket_handle)
{
    if (transport == NULL)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (!can_socket_transport_is_socket_valid(socket_handle))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_INVALID_ARGUMENT;
    }

    transport->socket_handle = socket_handle;
    transport->socket_attached = true;

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

void can_socket_transport_detach_socket(can_socket_transport_t *transport)
{
    if (transport != NULL)
    {
        transport->socket_handle = CAN_SOCKET_TRANSPORT_INVALID_SOCKET;
        transport->socket_attached = false;
    }
}

bool can_socket_transport_has_socket(const can_socket_transport_t *transport)
{
    return (transport != NULL) &&
           transport->socket_attached &&
           can_socket_transport_is_socket_valid(transport->socket_handle);
}

can_socket_transport_status_t can_socket_transport_serialize_frame(
    const can_frame_t *frame,
    uint8_t *buffer,
    size_t buffer_size)
{
    if ((frame == NULL) || (buffer == NULL))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (buffer_size < CAN_SOCKET_TRANSPORT_FRAME_SIZE)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_BUFFER_TOO_SMALL;
    }

    if (!can_frame_is_valid(frame))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_INVALID_FRAME;
    }

    (void)memset(buffer, 0, CAN_SOCKET_TRANSPORT_FRAME_SIZE);

    can_socket_transport_write_u32_be(
        &buffer[CAN_SOCKET_TRANSPORT_ID_OFFSET],
        frame->id);
    buffer[CAN_SOCKET_TRANSPORT_ID_TYPE_OFFSET] = (uint8_t)frame->id_type;
    buffer[CAN_SOCKET_TRANSPORT_FRAME_TYPE_OFFSET] = (uint8_t)frame->frame_type;
    buffer[CAN_SOCKET_TRANSPORT_DLC_OFFSET] = frame->dlc;
    buffer[CAN_SOCKET_TRANSPORT_VERSION_OFFSET] = CAN_SOCKET_TRANSPORT_PROTOCOL_VERSION;

    if ((frame->frame_type == CAN_FRAME_TYPE_DATA) && (frame->dlc > 0U))
    {
        (void)memcpy(
            &buffer[CAN_SOCKET_TRANSPORT_DATA_OFFSET],
            frame->data,
            frame->dlc);
    }

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

can_socket_transport_status_t can_socket_transport_deserialize_frame(
    const uint8_t *buffer,
    size_t buffer_size,
    can_frame_t *frame)
{
    if ((buffer == NULL) || (frame == NULL))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (buffer_size < CAN_SOCKET_TRANSPORT_FRAME_SIZE)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_BUFFER_TOO_SMALL;
    }

    if (buffer[CAN_SOCKET_TRANSPORT_VERSION_OFFSET] !=
        CAN_SOCKET_TRANSPORT_PROTOCOL_VERSION)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_INVALID_PAYLOAD;
    }

    can_frame_init(frame);

    frame->id = can_socket_transport_read_u32_be(
        &buffer[CAN_SOCKET_TRANSPORT_ID_OFFSET]);
    frame->id_type = (can_id_type_t)buffer[CAN_SOCKET_TRANSPORT_ID_TYPE_OFFSET];
    frame->frame_type = (can_frame_type_t)buffer[CAN_SOCKET_TRANSPORT_FRAME_TYPE_OFFSET];
    frame->dlc = buffer[CAN_SOCKET_TRANSPORT_DLC_OFFSET];

    if (frame->dlc > 0U)
    {
        (void)memcpy(
            frame->data,
            &buffer[CAN_SOCKET_TRANSPORT_DATA_OFFSET],
            CAN_FRAME_MAX_DATA_LEN);
    }

    if (!can_frame_is_valid(frame))
    {
        can_frame_init(frame);
        return CAN_SOCKET_TRANSPORT_STATUS_INVALID_FRAME;
    }

    if (frame->frame_type != CAN_FRAME_TYPE_DATA)
    {
        (void)memset(frame->data, 0, sizeof(frame->data));
    }
    else if (frame->dlc < CAN_FRAME_MAX_DATA_LEN)
    {
        (void)memset(
            &frame->data[frame->dlc],
            0,
            CAN_FRAME_MAX_DATA_LEN - frame->dlc);
    }

    return CAN_SOCKET_TRANSPORT_STATUS_OK;
}

can_socket_transport_status_t can_socket_transport_send_frame(
    const can_socket_transport_t *transport,
    const can_frame_t *frame)
{
    uint8_t buffer[CAN_SOCKET_TRANSPORT_FRAME_SIZE];
    can_socket_transport_status_t status;

    if ((transport == NULL) || (frame == NULL))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (!transport->network_initialized)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NOT_INITIALIZED;
    }

    if (!can_socket_transport_has_socket(transport))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_SOCKET_NOT_ATTACHED;
    }

    status = can_socket_transport_serialize_frame(
        frame,
        buffer,
        sizeof(buffer));
    if (status != CAN_SOCKET_TRANSPORT_STATUS_OK)
    {
        return status;
    }

    return can_socket_transport_send_all(
        transport->socket_handle,
        buffer,
        sizeof(buffer));
}

can_socket_transport_status_t can_socket_transport_receive_frame(
    const can_socket_transport_t *transport,
    can_frame_t *frame)
{
    uint8_t buffer[CAN_SOCKET_TRANSPORT_FRAME_SIZE];
    can_socket_transport_status_t status;

    if ((transport == NULL) || (frame == NULL))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER;
    }

    if (!transport->network_initialized)
    {
        return CAN_SOCKET_TRANSPORT_STATUS_NOT_INITIALIZED;
    }

    if (!can_socket_transport_has_socket(transport))
    {
        return CAN_SOCKET_TRANSPORT_STATUS_SOCKET_NOT_ATTACHED;
    }

    status = can_socket_transport_receive_all(
        transport->socket_handle,
        buffer,
        sizeof(buffer));
    if (status != CAN_SOCKET_TRANSPORT_STATUS_OK)
    {
        return status;
    }

    return can_socket_transport_deserialize_frame(
        buffer,
        sizeof(buffer),
        frame);
}
