import socket
import struct
import sys
import threading
import time
from typing import Optional, Tuple

UDP_BUS_HOST = "127.0.0.1"
UDP_BUS_PORT = 5000

REB_TCP_HOST = "127.0.0.1"
REB_TCP_PORT = 5001

UDP_BUFFER_SIZE = 1024

CAN_FRAME_SIZE = 16
CAN_PROTOCOL_VERSION = 1

SUPPORTED_RX_IDS = {
    0x200,  # REB_CMD
    0x300,  # TCU_TO_REB
    0x500,  # VEHICLE_STATE
    0x501,  # BCM_INTRUSION_STATUS
    0x502,  # PANEL_AUTH_CMD
    0x503,  # PANEL_CANCEL_CMD
}

SUPPORTED_TX_IDS = {
    0x201,  # REB_STATUS
    0x400,  # REB_DERATE_CMD
    0x401,  # REB_PREVENT_START
    0x402,  # REB_GPS_REQUEST
}


class REBBridge:
    def __init__(self) -> None:
        self.bus_addr = (UDP_BUS_HOST, UDP_BUS_PORT)
        self.reb_addr = (REB_TCP_HOST, REB_TCP_PORT)

        self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp_socket.bind((UDP_BUS_HOST, 0))
        self.udp_socket.settimeout(1.0)

        self.tcp_socket: Optional[socket.socket] = None
        self.running = False

    def start(self) -> None:
        self._connect_to_reb()

        self.running = True

        self._register_on_bus()

        print(
            f"[BRIDGE] Running. UDP bus={UDP_BUS_HOST}:{UDP_BUS_PORT} "
            f"<-> REB TCP={REB_TCP_HOST}:{REB_TCP_PORT}"
        )

        udp_thread = threading.Thread(target=self._udp_to_reb_loop, daemon=True)
        udp_thread.start()

        try:
            while self.running:
                time.sleep(0.2)
        except KeyboardInterrupt:
            print("\n[BRIDGE] Stopping...")
        finally:
            self.stop()

    def stop(self) -> None:
        self.running = False

        try:
            self.udp_socket.close()
        except Exception:
            pass

        if self.tcp_socket is not None:
            try:
                self.tcp_socket.close()
            except Exception:
                pass

        print("[BRIDGE] Stopped.")

    def _register_on_bus(self) -> None:
        try:
            self.udp_socket.sendto(b"000:REB_BRIDGE_CONNECTED", self.bus_addr)
            print("[BRIDGE] Registered on UDP bus")
        except Exception as exc:
            print(f"[BRIDGE] Failed to register on bus: {exc}")

    def _connect_to_reb(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(self.reb_addr)
        self.tcp_socket = sock
        print(f"[BRIDGE] Connected to REB at {self.reb_addr[0]}:{self.reb_addr[1]}")

    def _udp_to_reb_loop(self) -> None:
        while self.running:
            try:
                data, sender = self.udp_socket.recvfrom(UDP_BUFFER_SIZE)
            except socket.timeout:
                continue
            except OSError:
                break
            except Exception as exc:
                print(f"[BRIDGE] UDP receive error: {exc}")
                continue

            try:
                raw_msg = data.decode().strip()
            except Exception:
                continue

            parsed = self._parse_udp_can_message(raw_msg)
            if parsed is None:
                continue

            can_id, payload = parsed

            if can_id not in SUPPORTED_RX_IDS:
                continue

            try:
                frame = self._build_tcp_can_frame(can_id, payload)
                self._send_tcp_frame(frame)
                print(f"[BRIDGE] UDP -> REB | ID=0x{can_id:03X} DATA={payload.hex().upper()}")

                rx_frame = self._recv_tcp_frame()
                rx_id, rx_payload = self._parse_tcp_can_frame(rx_frame)

                if rx_id in SUPPORTED_TX_IDS:
                    udp_msg = self._build_udp_can_message(rx_id, rx_payload)
                    self.udp_socket.sendto(udp_msg.encode(), self.bus_addr)
                    print(f"[BRIDGE] REB -> UDP | {udp_msg}")

            except Exception as exc:
                print(f"[BRIDGE] Bridge transaction error: {exc}")
                self.running = False
                break

    def _parse_udp_can_message(self, raw_msg: str) -> Optional[Tuple[int, bytes]]:
        if ":" not in raw_msg:
            return None

        can_id_str, payload_hex = raw_msg.split(":", 1)
        can_id_str = can_id_str.strip()
        payload_hex = payload_hex.strip()

        if len(can_id_str) == 0:
            return None

        try:
            can_id = int(can_id_str, 16)
        except ValueError:
            return None

        if len(payload_hex) % 2 != 0:
            return None

        try:
            payload = bytes.fromhex(payload_hex)
        except ValueError:
            return None

        if len(payload) > 8:
            return None

        payload = payload.ljust(8, b"\x00")
        return can_id, payload

    def _build_udp_can_message(self, can_id: int, payload: bytes) -> str:
        return f"{can_id:03X}:{payload.hex().upper()}"

    def _build_tcp_can_frame(self, can_id: int, payload: bytes) -> bytes:
        payload = payload[:8].ljust(8, b"\x00")
        dlc = 8

        return struct.pack(
            ">IBBBB8s",
            can_id,                 # uint32 CAN ID
            0,                      # id_type = standard
            0,                      # frame_type = data
            dlc,                    # DLC
            CAN_PROTOCOL_VERSION,   # version
            payload,                # 8 bytes
        )

    def _parse_tcp_can_frame(self, frame: bytes) -> Tuple[int, bytes]:
        if len(frame) != CAN_FRAME_SIZE:
            raise ValueError(f"invalid TCP frame size: {len(frame)}")

        can_id, id_type, frame_type, dlc, version, payload = struct.unpack(
            ">IBBBB8s", frame
        )

        if version != CAN_PROTOCOL_VERSION:
            raise ValueError(f"invalid protocol version: {version}")

        if id_type != 0:
            raise ValueError(f"unsupported CAN id_type: {id_type}")

        if frame_type != 0:
            raise ValueError(f"unsupported CAN frame_type: {frame_type}")

        if dlc > 8:
            raise ValueError(f"invalid DLC: {dlc}")

        return can_id, payload[:dlc].ljust(8, b"\x00")

    def _send_tcp_frame(self, frame: bytes) -> None:
        if self.tcp_socket is None:
            raise RuntimeError("TCP socket not connected")

        self.tcp_socket.sendall(frame)

    def _recv_tcp_frame(self) -> bytes:
        if self.tcp_socket is None:
            raise RuntimeError("TCP socket not connected")

        chunks = b""
        while len(chunks) < CAN_FRAME_SIZE:
            piece = self.tcp_socket.recv(CAN_FRAME_SIZE - len(chunks))
            if not piece:
                raise ConnectionError("REB TCP connection closed")
            chunks += piece

        return chunks


def main() -> int:
    bridge = REBBridge()
    bridge.start()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"[BRIDGE] Fatal error: {exc}", file=sys.stderr)
        sys.exit(1)