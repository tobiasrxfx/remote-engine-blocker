#ifndef REB_CAN_SOCKET_TRANSPORT_H
#define REB_CAN_SOCKET_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef _WIN32
#include <winsock2.h>
typedef SOCKET can_socket_transport_socket_t;
#define CAN_SOCKET_TRANSPORT_INVALID_SOCKET    INVALID_SOCKET
#else
typedef int can_socket_transport_socket_t;
#define CAN_SOCKET_TRANSPORT_INVALID_SOCKET    (-1)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fixed serialized size for one transported CAN frame.
 *
 * Wire layout:
 * bytes 0..3  -> CAN ID (big-endian)
 * byte 4      -> can_id_type_t
 * byte 5      -> can_frame_type_t
 * byte 6      -> DLC
 * byte 7      -> protocol version
 * bytes 8..15 -> payload bytes
 */
#define CAN_SOCKET_TRANSPORT_FRAME_SIZE         (16U)
#define CAN_SOCKET_TRANSPORT_PROTOCOL_VERSION   (1U)

/**
 * @brief Status codes returned by the socket transport layer.
 */
typedef enum
{
    CAN_SOCKET_TRANSPORT_STATUS_OK = 0,
    CAN_SOCKET_TRANSPORT_STATUS_NULL_POINTER,
    CAN_SOCKET_TRANSPORT_STATUS_INVALID_ARGUMENT,
    CAN_SOCKET_TRANSPORT_STATUS_NOT_INITIALIZED,
    CAN_SOCKET_TRANSPORT_STATUS_SOCKET_NOT_ATTACHED,
    CAN_SOCKET_TRANSPORT_STATUS_SOCKET_ERROR,
    CAN_SOCKET_TRANSPORT_STATUS_CONNECTION_CLOSED,
    CAN_SOCKET_TRANSPORT_STATUS_BUFFER_TOO_SMALL,
    CAN_SOCKET_TRANSPORT_STATUS_INVALID_FRAME,
    CAN_SOCKET_TRANSPORT_STATUS_INVALID_PAYLOAD
} can_socket_transport_status_t;

/**
 * @brief Runtime context for the socket transport layer.
 *
 * The transport keeps only the communication state needed to
 * serialize frames and use an already-created socket.
 */
typedef struct
{
    bool network_initialized;
    bool socket_attached;
    can_socket_transport_socket_t socket_handle;
} can_socket_transport_t;

/**
 * @brief Initialize a socket transport context.
 *
 * @param transport Pointer to transport instance
 */
void can_socket_transport_init(can_socket_transport_t *transport);

/**
 * @brief Initialize the networking backend required by the transport.
 *
 * On Windows this wraps WSAStartup. On other platforms it only marks
 * the context as ready for use.
 *
 * @param transport Pointer to transport instance
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_startup(
    can_socket_transport_t *transport);

/**
 * @brief Release the networking backend associated with the transport.
 *
 * This function does not close the attached socket handle.
 *
 * @param transport Pointer to transport instance
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_shutdown(
    can_socket_transport_t *transport);

/**
 * @brief Attach an already-created socket to the transport.
 *
 * @param transport Pointer to transport instance
 * @param socket_handle Socket descriptor/handle
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_attach_socket(
    can_socket_transport_t *transport,
    can_socket_transport_socket_t socket_handle);

/**
 * @brief Detach the current socket handle from the transport.
 *
 * @param transport Pointer to transport instance
 */
void can_socket_transport_detach_socket(can_socket_transport_t *transport);

/**
 * @brief Check whether a valid socket handle is attached.
 *
 * @param transport Pointer to transport instance
 * @return true when a socket is attached
 */
bool can_socket_transport_has_socket(const can_socket_transport_t *transport);

/**
 * @brief Serialize a raw CAN frame into the socket wire format.
 *
 * @param frame Pointer to source CAN frame
 * @param buffer Output byte buffer
 * @param buffer_size Size of output buffer in bytes
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_serialize_frame(
    const can_frame_t *frame,
    uint8_t *buffer,
    size_t buffer_size);

/**
 * @brief Deserialize bytes received from the socket into a CAN frame.
 *
 * @param buffer Input byte buffer
 * @param buffer_size Size of input buffer in bytes
 * @param frame Output CAN frame
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_deserialize_frame(
    const uint8_t *buffer,
    size_t buffer_size,
    can_frame_t *frame);

/**
 * @brief Serialize and send one CAN frame through the attached socket.
 *
 * @param transport Pointer to transport instance
 * @param frame Pointer to CAN frame to send
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_send_frame(
    const can_socket_transport_t *transport,
    const can_frame_t *frame);

/**
 * @brief Receive and deserialize one CAN frame from the attached socket.
 *
 * @param transport Pointer to transport instance
 * @param frame Output CAN frame
 * @return Transport status code
 */
can_socket_transport_status_t can_socket_transport_receive_frame(
    const can_socket_transport_t *transport,
    can_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* REB_CAN_SOCKET_TRANSPORT_H */
