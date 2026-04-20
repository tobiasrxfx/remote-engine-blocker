import sys
import socket
import threading
import os
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton, QHBoxLayout, QFrame
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

    @Slot(str, str, str)
    def update_ui(self, can_id, payload, full_msg):
        current_lines = self.console.text().split("\n")[:8]
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
            mapping = {
                "00": ("IDLE", "gray"),
                "01": ("THEFT_CONFIRMED", "orange"),
                "02": ("BLOCKING", "red"),
                "03": ("BLOCKED", "darkred")
            }
            name, color = mapping.get(state_code, ("UNKNOWN", "white"))
            self.lbl_reb_state.setText(name)
            self.lbl_reb_state.setStyleSheet(f"font-size: 24px; font-weight: bold; color: {color};")
            
            self.bcm.update_alerts(state_code)
            
            alert_text = []
            if self.bcm.horn_active:
                alert_text.append("HORN")
            if self.bcm.hazard_lights_active:
                alert_text.append("HAZARDS")
            
            if alert_text:
                self.lbl_alerts.setText(f"ALERTS: {' & '.join(alert_text)} ACTIVE!")
                self.lbl_alerts.setStyleSheet("color: black; font-weight: bold;")
            else:
                self.lbl_alerts.setText("ALERTS: OFF")
                self.lbl_alerts.setStyleSheet("color: gray;")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())