import sys
import socket
import threading
import os
import time
import struct

from PySide6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QLabel,
    QPushButton,
    QHBoxLayout,
    QFrame,
    QSlider,
    QCheckBox,
    QSpinBox,
    QGridLayout,
)
from PySide6.QtCore import Qt, Signal, Slot

root_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
if root_path not in sys.path:
    sys.path.append(root_path)

from sim.bcm.bcm_module import BCMModule
from sim.tcu.tcu_module import TCUModule


class DashboardWindow(QMainWindow):
    can_msg_received = Signal(str, str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI - Remote Engine Blocker (GUI v2)")
        self.resize(1050, 820)

        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))

        self.bcm = BCMModule(self.socket, self.bus_addr)
        self.tcu = TCUModule(self.socket, self.bus_addr)

        self.sim_time_ms = 1000

        self.last_derate_percent = 0
        self.last_starter_lock = False
        self.last_tcu_status_tx = False
        self.engine_ecu_locked = False

        self.next_panel_nonce = 1

        self.init_ui()

        self.can_msg_received.connect(self.update_ui)
        self.listen_thread = threading.Thread(target=self.receive_can_frames, daemon=True)
        self.listen_thread.start()

        self.send_can_raw("000:DASHBOARD_CONNECTED")

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)

        # ===== TOP STATUS AREA =====
        top_layout = QHBoxLayout()

        # Speed
        self.speed_frame = QFrame()
        self.speed_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        speed_vbox = QVBoxLayout(self.speed_frame)
        speed_vbox.addWidget(QLabel("SPEED (KM/H)"))

        self.lbl_speed = QLabel("0.0")
        self.lbl_speed.setStyleSheet("font-size: 64px; font-weight: bold; color: #00FF00;")
        self.lbl_speed.setAlignment(Qt.AlignCenter)
        speed_vbox.addWidget(self.lbl_speed)

        top_layout.addWidget(self.speed_frame)

        # REB status
        self.status_frame = QFrame()
        self.status_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        status_vbox = QVBoxLayout(self.status_frame)
        status_vbox.addWidget(QLabel("REB SYSTEM STATUS"))

        self.lbl_reb_state = QLabel("IDLE")
        self.lbl_reb_state.setStyleSheet("font-size: 24px; font-weight: bold; color: gray;")
        self.lbl_reb_state.setAlignment(Qt.AlignCenter)
        status_vbox.addWidget(self.lbl_reb_state)

        self.lbl_alerts = QLabel("ALERTS: OFF")
        self.lbl_alerts.setStyleSheet("font-size: 14px; color: gray;")
        self.lbl_alerts.setAlignment(Qt.AlignCenter)
        status_vbox.addWidget(self.lbl_alerts)

        top_layout.addWidget(self.status_frame)

        # Actuation
        self.actuation_frame = QFrame()
        self.actuation_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        actuation_vbox = QVBoxLayout(self.actuation_frame)
        actuation_vbox.addWidget(QLabel("ACTUATION / ECU STATUS"))

        self.lbl_starter_lock = QLabel("Starter Lock: OFF")
        self.lbl_starter_lock.setStyleSheet("font-size: 14px; color: gray;")
        actuation_vbox.addWidget(self.lbl_starter_lock)

        self.lbl_derate = QLabel("Derate: 0%")
        self.lbl_derate.setStyleSheet("font-size: 14px; color: gray;")
        actuation_vbox.addWidget(self.lbl_derate)

        self.lbl_tcu_tx = QLabel("TCU Status TX: NO")
        self.lbl_tcu_tx.setStyleSheet("font-size: 14px; color: gray;")
        actuation_vbox.addWidget(self.lbl_tcu_tx)

        self.lbl_engine_ecu = QLabel("Engine ECU: UNLOCKED")
        self.lbl_engine_ecu.setStyleSheet("font-size: 14px; color: gray;")
        actuation_vbox.addWidget(self.lbl_engine_ecu)

        top_layout.addWidget(self.actuation_frame)

        layout.addLayout(top_layout)

        # ===== INPUT PANELS =====
        input_panels = QHBoxLayout()

        # Remote / panel commands
        self.commands_frame = QFrame()
        self.commands_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        commands_vbox = QVBoxLayout(self.commands_frame)
        commands_vbox.addWidget(QLabel("COMMAND INPUTS"))

        self.btn_remote_block = QPushButton("REMOTE BLOCK (0x200)")
        self.btn_remote_block.setStyleSheet("background-color: #AA0000; color: white; font-weight: bold;")
        self.btn_remote_block.clicked.connect(self.send_remote_block)
        commands_vbox.addWidget(self.btn_remote_block)

        self.btn_remote_unblock = QPushButton("REMOTE UNBLOCK (0x200)")
        self.btn_remote_unblock.setStyleSheet("background-color: #006600; color: white; font-weight: bold;")
        self.btn_remote_unblock.clicked.connect(self.send_remote_unblock)
        commands_vbox.addWidget(self.btn_remote_unblock)

        self.btn_panel_auth = QPushButton("PANEL AUTH (0x502)")
        self.btn_panel_auth.setStyleSheet("background-color: #4444AA; color: white; font-weight: bold;")
        self.btn_panel_auth.clicked.connect(self.send_panel_auth)
        commands_vbox.addWidget(self.btn_panel_auth)

        self.btn_panel_cancel = QPushButton("PANEL CANCEL (0x503)")
        self.btn_panel_cancel.setStyleSheet("background-color: #666666; color: white; font-weight: bold;")
        self.btn_panel_cancel.clicked.connect(self.send_panel_cancel)
        commands_vbox.addWidget(self.btn_panel_cancel)

        self.btn_bcm_intrusion = QPushButton("BCM INTRUSION (0x501)")
        self.btn_bcm_intrusion.setStyleSheet("background-color: #665500; color: white; font-weight: bold;")
        self.btn_bcm_intrusion.clicked.connect(self.send_bcm_intrusion)
        commands_vbox.addWidget(self.btn_bcm_intrusion)

        input_panels.addWidget(self.commands_frame)

        # Vehicle control
        self.vehicle_frame = QFrame()
        self.vehicle_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        vehicle_vbox = QVBoxLayout(self.vehicle_frame)
        vehicle_vbox.addWidget(QLabel("VEHICLE CONTROL"))

        self.lbl_speed_cmd = QLabel("Commanded speed: 0 km/h")
        vehicle_vbox.addWidget(self.lbl_speed_cmd)

        self.speed_slider = QSlider(Qt.Horizontal)
        self.speed_slider.setMinimum(0)
        self.speed_slider.setMaximum(150)
        self.speed_slider.setValue(0)
        self.speed_slider.valueChanged.connect(self.on_vehicle_control_changed)
        vehicle_vbox.addWidget(self.speed_slider)

        vehicle_grid = QGridLayout()

        self.chk_ignition = QCheckBox("Ignition ON")
        self.chk_ignition.setChecked(True)
        self.chk_ignition.stateChanged.connect(self.on_vehicle_control_changed)
        vehicle_grid.addWidget(self.chk_ignition, 0, 0)

        self.chk_engine_running = QCheckBox("Engine running")
        self.chk_engine_running.setChecked(True)
        self.chk_engine_running.stateChanged.connect(self.on_vehicle_control_changed)
        vehicle_grid.addWidget(self.chk_engine_running, 0, 1)

        vehicle_grid.addWidget(QLabel("RPM:"), 1, 0)
        self.spin_rpm = QSpinBox()
        self.spin_rpm.setRange(0, 8000)
        self.spin_rpm.setValue(2000)
        self.spin_rpm.valueChanged.connect(self.on_vehicle_control_changed)
        vehicle_grid.addWidget(self.spin_rpm, 1, 1)

        vehicle_vbox.addLayout(vehicle_grid)

        self.btn_send_vehicle = QPushButton("SEND VEHICLE_STATE (0x500)")
        self.btn_send_vehicle.setStyleSheet("background-color: #005555; color: white; font-weight: bold;")
        self.btn_send_vehicle.clicked.connect(self.send_vehicle_state)
        vehicle_vbox.addWidget(self.btn_send_vehicle)

        input_panels.addWidget(self.vehicle_frame)

        layout.addLayout(input_panels)

        # ===== TIME CONTROL =====
        self.time_frame = QFrame()
        self.time_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        time_layout = QVBoxLayout(self.time_frame)
        time_layout.addWidget(QLabel("SIMULATION TIME CONTROL"))

        self.lbl_sim_time = QLabel(f"{self.sim_time_ms} ms")
        self.lbl_sim_time.setStyleSheet("font-size: 18px; font-weight: bold; color: #00AAFF;")
        self.lbl_sim_time.setAlignment(Qt.AlignCenter)
        time_layout.addWidget(self.lbl_sim_time)

        time_row1 = QHBoxLayout()
        self.btn_tick_1s = QPushButton("+1s")
        self.btn_tick_1s.clicked.connect(lambda: self.send_time_tick(1000))
        time_row1.addWidget(self.btn_tick_1s)

        self.btn_tick_10s = QPushButton("+10s")
        self.btn_tick_10s.clicked.connect(lambda: self.send_time_tick(10000))
        time_row1.addWidget(self.btn_tick_10s)

        self.btn_tick_30s = QPushButton("+30s")
        self.btn_tick_30s.clicked.connect(lambda: self.send_time_tick(30000))
        time_row1.addWidget(self.btn_tick_30s)
        time_layout.addLayout(time_row1)

        time_row2 = QHBoxLayout()
        self.btn_tick_60s = QPushButton("+60s")
        self.btn_tick_60s.clicked.connect(lambda: self.send_time_tick(60000))
        time_row2.addWidget(self.btn_tick_60s)

        self.btn_tick_120s = QPushButton("+120s")
        self.btn_tick_120s.clicked.connect(lambda: self.send_time_tick(120000))
        time_row2.addWidget(self.btn_tick_120s)
        time_layout.addLayout(time_row2)

        time_row3 = QHBoxLayout()
        self.btn_go_blocking = QPushButton("GO TO BLOCKING")
        self.btn_go_blocking.setStyleSheet("background-color: orange; color: black; font-weight: bold;")
        self.btn_go_blocking.clicked.connect(self.jump_to_blocking)
        time_row3.addWidget(self.btn_go_blocking)

        self.btn_go_blocked = QPushButton("GO TO BLOCKED")
        self.btn_go_blocked.setStyleSheet("background-color: red; color: white; font-weight: bold;")
        self.btn_go_blocked.clicked.connect(self.jump_to_blocked)
        time_row3.addWidget(self.btn_go_blocked)
        time_layout.addLayout(time_row3)

        layout.addWidget(self.time_frame)

        # ===== CONSOLE =====
        self.console = QLabel("Awaiting telemetry...")
        self.console.setStyleSheet(
            "background-color: black; color: #00FF00; "
            "font-family: Consolas; padding: 10px;"
        )
        self.console.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        self.console.setWordWrap(True)
        self.console.setMinimumHeight(220)
        layout.addWidget(self.console)

    def send_can_raw(self, message):
        try:
            self.socket.sendto(message.encode(), self.bus_addr)
            print(f"[DASHBOARD] Sent: {message}")
        except Exception as e:
            print(f"[DASHBOARD ERROR] {e}")

    def _u16le_hex(self, value: int) -> str:
        return struct.pack("<H", value).hex().upper()

    def _u32le_hex(self, value: int) -> str:
        return struct.pack("<I", value).hex().upper()

    def _next_panel_nonce(self) -> int:
        value = self.next_panel_nonce
        self.next_panel_nonce += 1
        return value

    def on_vehicle_control_changed(self):
        self.lbl_speed_cmd.setText(f"Commanded speed: {self.speed_slider.value()} km/h")

    def send_remote_block(self):
        self.tcu.send_remote_block()

    def send_remote_unblock(self):
        self.tcu.send_remote_unblock()

    def send_panel_auth(self):
        auth_request = 1
        auth_method = 1
        auth_nonce = self._next_panel_nonce()

        payload = (
            f"{auth_request:02X}"
            f"{auth_method:02X}"
            f"{self._u16le_hex(auth_nonce)}"
            "00000000"
        )
        self.send_can_raw(f"502:{payload}")

    def send_panel_cancel(self):
        cancel_request = 1
        cancel_reason = 3
        cancel_nonce = self._next_panel_nonce()

        payload = (
            f"{cancel_request:02X}"
            f"{cancel_reason:02X}"
            f"{self._u16le_hex(cancel_nonce)}"
            "00000000"
        )
        self.send_can_raw(f"503:{payload}")

    def send_bcm_intrusion(self):
        self.bcm.trigger_intrusion()

    def send_vehicle_state(self):
        speed_kmh = self.speed_slider.value()
        speed_centi = int(speed_kmh * 100)

        ignition_on = 1 if self.chk_ignition.isChecked() else 0
        engine_running = 1 if self.chk_engine_running.isChecked() else 0
        rpm = self.spin_rpm.value()

        payload = (
            f"{self._u16le_hex(speed_centi)}"
            f"{ignition_on:02X}"
            f"{engine_running:02X}"
            f"{self._u16le_hex(rpm)}"
            "0000"
        )

        self.send_can_raw(f"500:{payload}")

    def send_time_tick(self, delta_ms):
        self.sim_time_ms += delta_ms
        self.lbl_sim_time.setText(f"{self.sim_time_ms} ms")

        payload = bytearray(8)
        payload[0] = 0x02  # CAN_TCU_CMD_RETRY
        payload[1] = 0x00
        payload[2:6] = self.sim_time_ms.to_bytes(4, "little")
        payload[6:8] = b"\x00\x00"

        self.send_can_raw(f"300:{payload.hex().upper()}")

    def jump_to_blocking(self):
        self.send_time_tick(70000)

    def jump_to_blocked(self):
        self.send_time_tick(200000)

    def update_starter_lock_ui(self, locked: bool):
        self.last_starter_lock = locked
        if locked:
            self.lbl_starter_lock.setText("Starter Lock: ON")
            self.lbl_starter_lock.setStyleSheet("font-size: 14px; color: red; font-weight: bold;")
        else:
            self.lbl_starter_lock.setText("Starter Lock: OFF")
            self.lbl_starter_lock.setStyleSheet("font-size: 14px; color: gray;")

    def update_derate_ui(self, derate_percent: int):
        self.last_derate_percent = derate_percent
        if derate_percent > 0:
            self.lbl_derate.setText(f"Derate: {derate_percent}%")
            self.lbl_derate.setStyleSheet("font-size: 14px; color: orange; font-weight: bold;")
        else:
            self.lbl_derate.setText("Derate: 0%")
            self.lbl_derate.setStyleSheet("font-size: 14px; color: gray;")

    def update_tcu_tx_ui(self, active: bool):
        self.last_tcu_status_tx = active
        if active:
            self.lbl_tcu_tx.setText("TCU Status TX: YES")
            self.lbl_tcu_tx.setStyleSheet("font-size: 14px; color: #00AAFF; font-weight: bold;")
        else:
            self.lbl_tcu_tx.setText("TCU Status TX: NO")
            self.lbl_tcu_tx.setStyleSheet("font-size: 14px; color: gray;")

    def update_engine_ecu_ui(self, locked: bool):
        self.engine_ecu_locked = locked
        if locked:
            self.lbl_engine_ecu.setText("Engine ECU: LOCKED")
            self.lbl_engine_ecu.setStyleSheet("font-size: 14px; color: darkred; font-weight: bold;")
        else:
            self.lbl_engine_ecu.setText("Engine ECU: UNLOCKED")
            self.lbl_engine_ecu.setStyleSheet("font-size: 14px; color: gray;")

    def receive_can_frames(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                raw_msg = data.decode()
                if ":" in raw_msg:
                    can_id, payload = raw_msg.split(":", 1)
                    self.can_msg_received.emit(can_id.upper(), payload.upper(), raw_msg)
            except ConnectionResetError as e:
                print(f"[DASHBOARD] Ignoring UDP reset on Windows: {e}")
                continue
            except OSError as e:
                print(f"[DASHBOARD] Socket warning: {e}")
                continue
            except Exception as e:
                print(f"Receive Error: {e}")
                break

    @Slot(str, str, str)
    def update_ui(self, can_id, payload, full_msg):
        current_lines = self.console.text().split("\n")[:12]
        self.console.setText(f"[RX] {full_msg}\n" + "\n".join(current_lines))

        if can_id == "500":
            try:
                speed_centi_kmh = int.from_bytes(bytes.fromhex(payload[:4]), "little")
                speed_kmh = speed_centi_kmh / 100.0
                self.lbl_speed.setText(f"{speed_kmh:.1f}")
            except Exception:
                pass

        elif can_id == "201":
            state_code = payload[:2]
            blocked_flag_hex = payload[2:4]

            mapping = {
                "00": ("IDLE", "gray"),
                "01": ("THEFT_CONFIRMED", "orange"),
                "02": ("BLOCKING", "red"),
                "03": ("BLOCKED", "darkred")
            }
            name, color = mapping.get(state_code, ("UNKNOWN", "white"))
            self.lbl_reb_state.setText(name)
            self.lbl_reb_state.setStyleSheet(
                f"font-size: 24px; font-weight: bold; color: {color};"
            )

            self.bcm.update_alerts(state_code)

            alert_text = []
            if self.bcm.horn_active:
                alert_text.append("HORN")
            if self.bcm.hazard_lights_active:
                alert_text.append("HAZARDS")

            if alert_text:
                self.lbl_alerts.setText(f"ALERTS: {' & '.join(alert_text)} ACTIVE!")
                self.lbl_alerts.setStyleSheet("color: gray; font-weight: bold;")
            else:
                self.lbl_alerts.setText("ALERTS: OFF")
                self.lbl_alerts.setStyleSheet("color: gray;")

            try:
                blocked_flag = int(blocked_flag_hex, 16)
                self.update_starter_lock_ui(blocked_flag == 1)
            except Exception:
                pass

            self.update_tcu_tx_ui(True)

        elif can_id == "400":
            try:
                derate_percent = int(payload[:2], 16)
                self.update_derate_ui(derate_percent)
            except Exception:
                pass

        elif can_id == "401":
            try:
                prevent_start = int(payload[:2], 16)
                locked = (prevent_start == 1)
                self.update_engine_ecu_ui(locked)
                self.update_starter_lock_ui(locked)
            except Exception:
                pass


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())