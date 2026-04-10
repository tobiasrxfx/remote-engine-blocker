import socket

class TCUModule:
    def __init__(self, socket_ref=None, bus_addr=('127.0.0.1', 5000)):
        self.socket = socket_ref
        self.bus_addr = bus_addr

    def send_remote_block(self):
        # ID 0x200: REB_CMD 
        # cmd_type = 1 (BLOCK), cmd_mode = 2 (FULL_BLOCK)
        # Payload: 01 (BLOCK) + 02 (FULL) + 000000 (padding/nonce)
        message = "200:01020000"
        if self.socket:
            try:
                self.socket.sendto(message.encode(), self.bus_addr)
                print(f" [TCU] Remote Block Command sent: {message}")
            except Exception as e:
                print(f" [TCU ERROR] {e}")

    def send_remote_unblock(self):
        # ID 0x200: cmd_type = 2 (UNBLOCK)
        message = "200:02000000"
        if self.socket:
            try:
                self.socket.sendto(message.encode(), self.bus_addr)
                print(f" [TCU] Remote Unblock Command sent: {message}")
            except Exception as e:
                print(f" [TCU ERROR] {e}")

if __name__ == "__main__":
    # Standalone test
    test_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tcu = TCUModule(test_socket)
    tcu.send_remote_block()