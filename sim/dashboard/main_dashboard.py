import sys
import socket
import threading
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton, QHBoxLayout, QFrame
from PySide6.QtCore import Qt, Signal, Slot

class DashboardWindow(QMainWindow):
    # Signal to update UI from the background socket thread
    can_msg_received = Signal(str, str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI - Remote Engine Blocker (SWT3)")
        self.resize(700, 500)

        # Network configuration
        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))

        self.init_ui()

        # Listen for CAN traffic in background
        self.listen_thread = threading.Thread(target=self.receive_can_frames, daemon=True)
        self.listen_thread.start()

        # Automatic registration in virtual bus
        self.socket.sendto(b"000:DASHBOARD_CONNECTED", self.bus_addr)

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)

        # Telemetry Display
        telemetry_layout = QHBoxLayout()

        # Speedometer
        self.speed_frame = QFrame()
        self.speed_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        speed_vbox = QVBoxLayout(self.speed_frame)
        self.lbl_speed = QLabel("0")
        self.lbl_speed.setStyleSheet("font-size: 72px; font-weight: bold; color: #00FF00;")
        self.lbl_speed.setAlignment(Qt.AlignCenter)
        speed_vbox.addWidget(QLabel("SPEED (KM/H)"))
        speed_vbox.addWidget(self.lbl_speed)
        telemetry_layout.addWidget(self.speed_frame)

        # REB Status
        self.status_frame = QFrame()
        self.status_frame.setFrameStyle(QFrame.StyledPanel | QFrame.Sunken)
        status_vbox = QVBoxLayout(self.status_frame)
        self.lbl_reb_state = QLabel("IDLE")
        self.lbl_reb_state.setStyleSheet("font-size: 24px; font-weight: bold; color: gray;")
        self.lbl_reb_state.setAlignment(Qt.AlignCenter)
        status_vbox.addWidget(QLabel("REB SYSTEM STATUS"))
        status_vbox.addWidget(self.lbl_reb_state)
        telemetry_layout.addWidget(self.status_frame)

        layout.addLayout(telemetry_layout)

        # Control Button (Simulates TCU 0x200 command) [cite: 3, 5]
        self.btn_theft = QPushButton("SIMULATE THEFT (SEND BLOCK)")
        self.btn_theft.setFixedHeight(50)
        self.btn_theft.setStyleSheet("background-color: #AA0000; color: white; font-weight: bold;")
        self.btn_theft.clicked.connect(self.send_theft_command)
        layout.addWidget(self.btn_theft)

        # Log Console
        self.console = QLabel("Awaiting telemetry...")
        self.console.setStyleSheet("background-color: black; color: #00FF00; font-family: Consolas; padding: 10px;")
        self.console.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        self.console.setWordWrap(True)
        layout.addWidget(self.console)

        self.can_msg_received.connect(self.update_ui)

    def send_theft_command(self):
        # Send ID 0x200 (REB_CMD), cmd_type = 1 (BLOCK) [cite: 3, 9]
        message = "200:01000000"
        self.socket.sendto(message.encode(), self.bus_addr)

    def receive_can_frames(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                raw_msg = data.decode()
                if ":" in raw_msg:
                    can_id, payload = raw_msg.split(":")
                    self.can_msg_received.emit(can_id, payload, raw_msg)
            except:
                break

    @Slot(str, str, str)
    def update_ui(self, can_id, payload, full_msg):
        # Update log
        current_log = self.console.text().split("\n")[:10]
        self.console.setText(f"[RX] {full_msg}\n" + "\n".join(current_log))

        # Process Speed (Simulated ID 0x500)
        if can_id == "500":
            speed_hex = payload[:2]
            speed_decimal = int(speed_hex, 16)
            self.lbl_speed.setText(str(speed_decimal))

        # Process REB Status (ID 0x201) [cite: 3, 9]
        elif can_id == "201":
            state_code = payload[:2]
            # Mapping based on Section 6 of can_messages.txt [cite: 9]
            mapping = {
                "00": ("IDLE", "gray"),
                "01": ("THEFT_CONFIRMED", "orange"),
                "02": ("BLOCKING", "red"),
                "03": ("BLOCKED", "darkred")
            }
            name, color = mapping.get(state_code, ("UNKNOWN", "white"))
            self.lbl_reb_state.setText(name)
            self.lbl_reb_state.setStyleSheet(f"font-size: 24px; font-weight: bold; color: {color};")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())
