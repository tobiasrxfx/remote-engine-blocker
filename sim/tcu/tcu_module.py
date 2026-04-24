import socket
import time
import struct

class TCUModule:
    def __init__(self, socket_ref=None, bus_addr=('127.0.0.1', 5000)):
        self.socket = socket_ref
        self.bus_addr = bus_addr
        self.next_nonce = 1

    def _u16le_hex(self, value: int) -> str:
        return struct.pack("<H", value).hex().upper()

    def _u32le_hex(self, value: int) -> str:
        return struct.pack("<I", value).hex().upper()

    def _next_timestamp_ms(self) -> int:
        return int(time.time() * 1000) & 0xFFFFFFFF
    
    def send_remote_block(self):
        """
        REB_CMD (0x200):
          byte 0 = cmd_type      (1 = BLOCK)
          byte 1 = cmd_mode      (2 = FULL_BLOCK)
          byte 2..3 = cmd_nonce  (u16 LE)
          byte 4..7 = timestamp  (u32 LE)
        """
        cmd_type = 1
        cmd_mode = 2
        nonce = self.next_nonce
        timestamp = self._next_timestamp_ms()

        payload = (
            f"{cmd_type:02X}"
            f"{cmd_mode:02X}"
            f"{self._u16le_hex(nonce)}"
            f"{self._u32le_hex(timestamp)}"
        )

        message = f"200:{payload}"

        if self.socket:
            try:
                self.socket.sendto(message.encode(), self.bus_addr)
                print(f" [TCU] Remote Block Command sent: {message}")
                self.next_nonce += 1
            except Exception as e:
                print(f" [TCU ERROR] {e}")

    def send_remote_unblock(self):
        """
        REB_CMD (0x200):
          byte 0 = cmd_type      (2 = UNBLOCK)
          byte 1 = cmd_mode      (0 = IDLE/default)
          byte 2..3 = cmd_nonce  (u16 LE)
          byte 4..7 = timestamp  (u32 LE)
        """
        cmd_type = 2
        cmd_mode = 0
        nonce = self.next_nonce
        timestamp = self._next_timestamp_ms()

        payload = (
            f"{cmd_type:02X}"
            f"{cmd_mode:02X}"
            f"{self._u16le_hex(nonce)}"
            f"{self._u32le_hex(timestamp)}"
        )

        message = f"200:{payload}"

        if self.socket:
            try:
                self.socket.sendto(message.encode(), self.bus_addr)
                print(f" [TCU] Remote Unblock Command sent: {message}")
                self.next_nonce += 1
            except Exception as e:
                print(f" [TCU ERROR] {e}")
    
    def send_tcu_ack(self):
        """
        Sends TCU_TO_REB ACK (0x300), needed by reb_security_unlock_allowed().
        """
        tcu_cmd_ack = 0  # CAN_TCU_CMD_ACK
        fail_reason = 0
        echo_timestamp = self._next_timestamp_ms()

        payload = (
            f"{tcu_cmd_ack:02X}"
            f"{fail_reason:02X}"
            f"{self._u32le_hex(echo_timestamp)}"
            "0000"
        )

        message = f"300:{payload}"

        if self.socket:
            try:
                self.socket.sendto(message.encode(), self.bus_addr)
                print(f"[TCU] ACK sent: {message}")
            except Exception as e:
                print(f"[TCU ERROR] {e}")

if __name__ == "__main__":
    # Standalone test
    test_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tcu = TCUModule(test_socket)
    tcu.send_remote_block()