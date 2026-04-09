import socket
import sys

# Virtual CAN Configuration
BUS_HOST = '127.0.0.1'
BUS_PORT = 5000
BUFFER_SIZE = 1024

def start_virtual_bus():
    """
    Simulates a CAN Bus using UDP Sockets.
    Acts as a hub that broadcasts any received message to all connected modules.
    """
    # Create a UDP socket
    bus_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    try:
        bus_socket.bind((BUS_HOST, BUS_PORT))
        # Set timeout to 1.0s to allow KeyboardInterrupt (Ctrl+C) to work on Windows
        bus_socket.settimeout(1.0)
    except Exception as e:
        print(f" [ERROR] Could not start bus: {e}")
        sys.exit(1)

    # Store addresses of connected modules (GUI, TCU, REB, ECUs)
    clients = set()

    print(f"--- Virtual CAN Bus Started on {BUS_HOST}:{BUS_PORT} ---")
    print(" [INFO] Press Ctrl+C to shut down safely.")
    print(" Waiting for modules to connect...")

    try:
        while True:
            try:
                # Receive data and sender address
                data, addr = bus_socket.recvfrom(BUFFER_SIZE)
            except socket.timeout:
                # Timeout reached, loop back to check for Ctrl+C
                continue

            # Register new client addresses automatically upon first message
            if addr not in clients:
                clients.add(addr)
                print(f" [BUS] New module detected at {addr}")

            # Broadcast the received frame to ALL other connected clients
            # This simulates the shared nature of a real CAN bus
            message = data.decode().strip()
            for client_addr in clients:
                if client_addr != addr:
                    try:
                        bus_socket.sendto(data, client_addr)
                    except Exception as e:
                        print(f" [BUS] Failed to send to {client_addr}: {e}")

    except KeyboardInterrupt:
        print("\n--- Virtual CAN Bus Shutting Down ---")
    finally:
        bus_socket.close()
        print(" [SYS] Socket closed successfully.")

if __name__ == "__main__":
    start_virtual_bus()
