import sys
import socket
import threading
import os
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton, QHBoxLayout, QFrame, QSlider
from PySide6.QtCore import Qt, Signal, Slot

# Path setup to ensure we can import from directories (bcm, bus)
root_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
if root_path not in sys.path:
    sys.path.append(root_path)

from sim.bcm.bcm_module import BCMModule
from sim.tcu.tcu_module import TCUModule

class DashboardWindow(QMainWindow):
    can_msg_received = Signal(str, str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI - Remote Engine Blocker (SWT3)")
        self.resize(700, 550)


        # Configuration for Virtual CAN Bus
        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # O bind em 0 permite que o SO escolha a porta, igual ao vehicle_plant
        self.socket.bind(('127.0.0.1', 0))

        # Modules
        self.bcm = BCMModule(self.socket, self.bus_addr)
        self.tcu = TCUModule(self.socket, self.bus_addr)

        self.sim_time_ms = 1000

        self.last_derate_percent = 0
        self.last_starter_lock = False
        self.last_tcu_status_tx = False
        self.engine_ecu_locked = False

        # UI Setup
        self.init_ui()

        # Comunnication Setup
        self.can_msg_received.connect(self.update_ui)
        self.listen_thread = threading.Thread(target=self.receive_can_frames, daemon=True)
        self.listen_thread.start()

        # Initial message to identify dashboard on the bus
        self.send_can_raw("000:DASHBOARD_CONNECTED")

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)

        # Telemetry Display
        telemetry_layout = QHBoxLayout()
        
        # Velocity Display
        self.speed_frame = QFrame()
        self.speed_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        speed_vbox = QVBoxLayout(self.speed_frame)
        self.lbl_speed = QLabel("0")
        self.lbl_speed.setStyleSheet("font-size: 72px; font-weight: bold; color: #00FF00;")
        self.lbl_speed.setAlignment(Qt.AlignCenter)
        speed_vbox.addWidget(QLabel("SPEED (KM/H)"))
        speed_vbox.addWidget(self.lbl_speed)
        telemetry_layout.addWidget(self.speed_frame)

        # Status Display
        self.status_frame = QFrame()
        self.status_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        status_vbox = QVBoxLayout(self.status_frame)
        self.lbl_reb_state = QLabel("IDLE")
        self.lbl_reb_state.setStyleSheet("font-size: 24px; font-weight: bold; color: gray;")
        self.lbl_reb_state.setAlignment(Qt.AlignCenter)
        
        self.lbl_alerts = QLabel("ALERTS: OFF")
        self.lbl_alerts.setStyleSheet("font-size: 14px; color: gray;")
        self.lbl_alerts.setAlignment(Qt.AlignCenter)
        
        status_vbox.addWidget(QLabel("REB SYSTEM STATUS"))
        status_vbox.addWidget(self.lbl_reb_state)
        status_vbox.addWidget(self.lbl_alerts)
        telemetry_layout.addWidget(self.status_frame)

        layout.addLayout(telemetry_layout)

        # Actuation / ECU status display
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

        layout.addWidget(self.actuation_frame)

        # Buttons
        # BCM Button
        self.btn_bcm_theft = QPushButton("SIMULATE BCM INTRUSION (0x501)")
        self.btn_bcm_theft.setFixedHeight(50)
        self.btn_bcm_theft.setStyleSheet("background-color: #555500; color: white; font-weight: bold;")
        # Usamos lambda para garantir que o clique chame a função corretamente
        self.btn_bcm_theft.clicked.connect(lambda: self.bcm.trigger_intrusion())
        layout.addWidget(self.btn_bcm_theft)

        # TCU Button
        self.btn_theft = QPushButton("SIMULATE REMOTE THEFT (0x200)")
        self.btn_theft.setFixedHeight(50)
        self.btn_theft.setStyleSheet("background-color: #AA0000; color: white; font-weight: bold;")
        self.btn_theft.clicked.connect(self.send_theft_command)
        layout.addWidget(self.btn_theft)

        # Console Log
        self.console = QLabel("Awaiting telemetry...")
        self.console.setStyleSheet("background-color: black; color: #00FF00; font-family: Consolas; padding: 10px;")
        self.console.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        self.console.setWordWrap(True)
        self.console.setMinimumHeight(150)
        layout.addWidget(self.console)

        # Vehicle control
        self.vehicle_frame = QFrame()
        self.vehicle_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        vehicle_layout = QVBoxLayout(self.vehicle_frame)

        vehicle_layout.addWidget(QLabel("VEHICLE SPEED CONTROL"))

        self.speed_slider = QSlider(Qt.Horizontal)
        self.speed_slider.setMinimum(0)
        self.speed_slider.setMaximum(150)  # km/h
        self.speed_slider.setValue(0)
        self.speed_slider.valueChanged.connect(self.send_vehicle_speed)

        vehicle_layout.addWidget(self.speed_slider)

        layout.addWidget(self.vehicle_frame)

        # Time control
        self.time_frame = QFrame()
        self.time_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        time_layout = QVBoxLayout(self.time_frame)

        time_layout.addWidget(QLabel("SIMULATION TIME CONTROL"))

        self.lbl_sim_time = QLabel(f"{self.sim_time_ms} ms")
        self.lbl_sim_time.setStyleSheet("font-size: 18px; font-weight: bold; color: #00AAFF;")
        self.lbl_sim_time.setAlignment(Qt.AlignCenter)
        time_layout.addWidget(self.lbl_sim_time)

        # First row of time buttons
        time_buttons_row1 = QHBoxLayout()

        self.btn_tick_1s = QPushButton("+1s")
        self.btn_tick_1s.setFixedHeight(40)
        self.btn_tick_1s.setStyleSheet("background-color: #003366; color: white; font-weight: bold;")
        self.btn_tick_1s.clicked.connect(lambda: self.send_time_tick(1000))
        time_buttons_row1.addWidget(self.btn_tick_1s)

        self.btn_tick_10s = QPushButton("+10s")
        self.btn_tick_10s.setFixedHeight(40)
        self.btn_tick_10s.setStyleSheet("background-color: #004C99; color: white; font-weight: bold;")
        self.btn_tick_10s.clicked.connect(lambda: self.send_time_tick(10000))
        time_buttons_row1.addWidget(self.btn_tick_10s)

        self.btn_tick_30s = QPushButton("+30s")
        self.btn_tick_30s.setFixedHeight(40)
        self.btn_tick_30s.setStyleSheet("background-color: #0066CC; color: white; font-weight: bold;")
        self.btn_tick_30s.clicked.connect(lambda: self.send_time_tick(30000))
        time_buttons_row1.addWidget(self.btn_tick_30s)

        time_layout.addLayout(time_buttons_row1)

        # Second row of time buttons
        time_buttons_row2 = QHBoxLayout()

        self.btn_tick_60s = QPushButton("+60s")
        self.btn_tick_60s.setFixedHeight(40)
        self.btn_tick_60s.setStyleSheet("background-color: #0080FF; color: black; font-weight: bold;")
        self.btn_tick_60s.clicked.connect(lambda: self.send_time_tick(60000))
        time_buttons_row2.addWidget(self.btn_tick_60s)

        self.btn_tick_120s = QPushButton("+120s")
        self.btn_tick_120s.setFixedHeight(40)
        self.btn_tick_120s.setStyleSheet("background-color: #66B3FF; color: black; font-weight: bold;")
        self.btn_tick_120s.clicked.connect(lambda: self.send_time_tick(120000))
        time_buttons_row2.addWidget(self.btn_tick_120s)

        time_layout.addLayout(time_buttons_row2)

        # Scenario shortcuts
        shortcut_buttons = QHBoxLayout()

        self.btn_go_blocking = QPushButton("GO TO BLOCKING")
        self.btn_go_blocking.setFixedHeight(40)
        self.btn_go_blocking.setStyleSheet("background-color: orange; color: black; font-weight: bold;")
        self.btn_go_blocking.clicked.connect(self.jump_to_blocking)
        shortcut_buttons.addWidget(self.btn_go_blocking)

        self.btn_go_blocked = QPushButton("GO TO BLOCKED")
        self.btn_go_blocked.setFixedHeight(40)
        self.btn_go_blocked.setStyleSheet("background-color: red; color: white; font-weight: bold;")
        self.btn_go_blocked.clicked.connect(self.jump_to_blocked)
        shortcut_buttons.addWidget(self.btn_go_blocked)

        time_layout.addLayout(shortcut_buttons)
        layout.addWidget(self.time_frame)

    def send_can_raw(self, message):
        """Sends a raw CAN message to the bus."""
        try:
            self.socket.sendto(message.encode(), self.bus_addr)
            print(f"[DASHBOARD] Sent: {message}") 
        except Exception as e:
            print(f"[DASHBOARD ERROR] {e}")

    def send_theft_command(self):
        self.tcu.send_remote_block()

    def receive_can_frames(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                raw_msg = data.decode()
                if ":" in raw_msg:
                    can_id, payload = raw_msg.split(":",1)
                    self.can_msg_received.emit(can_id.upper(), payload.upper(), raw_msg)
            except Exception as e:
                print(f"Receive Error: {e}")
                break


    def send_vehicle_speed(self):
        speed_kmh = self.speed_slider.value()
        speed_centi = int(speed_kmh * 100)

        payload = (
            speed_centi.to_bytes(2, "little").hex()
            + "01"  # ignition ON
            + "01"  # engine running
            + (2000).to_bytes(2, "little").hex()  # rpm
            + "0000"
        )

        msg = f"500:{payload.upper()}"
        self.send_can_raw(msg)

    def send_time_tick(self, delta_ms):
        """
        Sends a TCU_TO_REB message used by the loop server as a simulation time tick.

        Convention used by the REB loop server:
          ID 0x300
          byte 0 = CAN_TCU_CMD_RETRY (2)
          byte 1 = fail_reason (0)
          byte 2..5 = echo_timestamp (u32 little-endian) = new absolute time
          byte 6..7 = padding
        """
        self.sim_time_ms += delta_ms
        self.lbl_sim_time.setText(f"{self.sim_time_ms} ms")

        payload = bytearray(8)
        payload[0] = 0x02  # CAN_TCU_CMD_RETRY
        payload[1] = 0x00
        payload[2:6] = self.sim_time_ms.to_bytes(4, "little")
        payload[6:8] = b"\x00\x00"

        msg = f"300:{payload.hex().upper()}"
        self.send_can_raw(msg)

    def jump_to_blocking(self):
        """
        Practical shortcut for demos:
        advance enough time to likely leave THEFT_CONFIRMED
        and reach BLOCKING.
        """
        self.send_time_tick(70000)

    def jump_to_blocked(self):
        """
        Practical shortcut for demos:
        advance enough time to likely pass both:
        - theft confirmation window
        - stop hold / blocking dwell
        """
        self.send_time_tick(200000)

    @Slot(str, str, str)
    def update_ui(self, can_id, payload, full_msg):
        current_lines = self.console.text().split("\n")[:10]
        self.console.setText(f"[RX] {full_msg}\n" + "\n".join(current_lines))

        if can_id == "500":
            try:
                speed_centi_kmh = int.from_bytes(bytes.fromhex(payload[:4]), "little")
                speed_kmh = speed_centi_kmh / 100.0
                self.lbl_speed.setText(f"{speed_kmh:.1f}")
            except Exception:
                pass

        elif can_id == "201":
            # REB_STATUS
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
                self.lbl_alerts.setStyleSheet("color: yellow; font-weight: bold;")
            else:
                self.lbl_alerts.setText("ALERTS: OFF")
                self.lbl_alerts.setStyleSheet("color: gray;")

            try:
                blocked_flag = int(blocked_flag_hex, 16)
                self.update_starter_lock_ui(blocked_flag == 1)
            except Exception:
                pass

            # if a status frame arrived, consider TX active
            self.update_tcu_tx_ui(True)

        elif can_id == "400":
            # REB_DERATE_CMD
            try:
                derate_percent = int(payload[:2], 16)
                self.update_derate_ui(derate_percent)
            except Exception:
                pass

        elif can_id == "401":
            # REB_PREVENT_START
            try:
                prevent_start = int(payload[:2], 16)
                locked = (prevent_start == 1)
                self.update_engine_ecu_ui(locked)
                self.update_starter_lock_ui(locked)
            except Exception:
                pass
    
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

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())