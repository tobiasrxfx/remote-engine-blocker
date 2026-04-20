import socket
import threading
import time


class EngineECU:
    """
    Simulated Engine ECU.
    Listens on the UDP virtual CAN bus and reacts to REB_PREVENT_START (0x401).
    """

    def __init__(self, bus_addr=('127.0.0.1', 5000)):
        self.bus_addr = bus_addr
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))  # random local port

        self.starter_locked = False
        self.last_auth_token_lsb = 0

        # Register on the bus
        self.socket.sendto(b"000:ENGINE_ECU_CONNECTED", self.bus_addr)

        # Background listener
        self.listener_thread = threading.Thread(target=self.receive_can, daemon=True)
        self.listener_thread.start()

    def receive_can(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                msg = data.decode().strip()

                if not msg.startswith("401:"):
                    continue

                payload = msg.split(":", 1)[1].strip().upper()

                if len(payload) < 4:
                    print(f"[ENGINE_ECU] Invalid 0x401 payload: {payload}")
                    continue

                # REB_PREVENT_START layout:
                # byte 0 = prevent_start
                # byte 1 = auth_token_lsb
                prevent_start = int(payload[0:2], 16)
                auth_token_lsb = int(payload[2:4], 16)

                self.last_auth_token_lsb = auth_token_lsb

                previous_state = self.starter_locked
                self.starter_locked = (prevent_start == 1)

                print(
                    f"[ENGINE_ECU] REB_PREVENT_START received | "
                    f"prevent_start={prevent_start} "
                    f"auth_token_lsb=0x{auth_token_lsb:02X}"
                )

                if self.starter_locked != previous_state:
                    if self.starter_locked:
                        print("[ENGINE_ECU] Starter is now LOCKED")
                    else:
                        print("[ENGINE_ECU] Starter is now UNLOCKED")

            except Exception as e:
                print(f"[ENGINE_ECU] Error: {e}")
                break

    def get_status(self):
        return {
            "starter_locked": self.starter_locked,
            "last_auth_token_lsb": self.last_auth_token_lsb,
        }


if __name__ == "__main__":
    ecu = EngineECU()
    print("Engine ECU active - waiting for REB_PREVENT_START (0x401)...")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nEngine ECU stopped.")