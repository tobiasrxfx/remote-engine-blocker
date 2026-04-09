import sys
import socket
import threading
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton, QHBoxLayout
from PySide6.QtCore import Qt, Signal, Slot

class DashboardWindow(QMainWindow):
    # Signal to update UI from the background socket thread
    can_message_received = Signal(str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI - REB Virtual Vehicle Platform")
        self.resize(600, 450)

        # Networking setup
        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Bind to port 0 to let OS choose an available one for receiving
        self.socket.bind(('127.0.0.1', 0))

        # UI Elements
        self.init_ui()

        # Start background thread to listen to the bus
        self.can_message_received.connect(self.update_ui_from_can)
        self.listen_thread = threading.Thread(target=self.receive_can_frames, daemon=True)
        self.listen_thread.start()

        # AUTOMATIC PING: Register this GUI in the virtual bus immediately
        self.send_initial_ping()

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        layout = QVBoxLayout(central_widget)

        self.lbl_reb_state = QLabel("REB State: IDLE (Waiting for 0x201...)")
        self.lbl_reb_state.setStyleSheet("font-size: 16px; font-weight: bold; color: gray;")
        layout.addWidget(self.lbl_reb_state)

        # Controls
        ctrl_layout = QHBoxLayout()
        self.btn_theft = QPushButton("SEND REMOTE THEFT (0x200)")
        self.btn_theft.clicked.connect(self.send_theft_command)
        ctrl_layout.addWidget(self.btn_theft)
        layout.addLayout(ctrl_layout)

        # Console
        self.console = QLabel("CAN Traffic Console:\n")
        self.console.setStyleSheet("background-color: black; color: #00FF00; font-family: Consolas; padding: 10px;")
        self.console.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        self.console.setWordWrap(True)
        layout.addWidget(self.console)

    def send_initial_ping(self):
        """Sends a startup message to register the GUI address in the Virtual Bus."""
        startup_msg = "000:GUI_CONNECTED"
        try:
            self.socket.sendto(startup_msg.encode(), self.bus_addr)
            self.can_message_received.emit("SYS", "GUI registered in virtual bus.")
        except Exception as e:
            self.can_message_received.emit("ERR", f"Failed to connect to bus: {e}")

    def send_theft_command(self):
        # ID 0x200 (REB_CMD), cmd_type = 1 (BLOCK) [cite: 712, 713, 718]
        message = "200:01000000"
        self.socket.sendto(message.encode(), self.bus_addr)
        self.can_message_received.emit("TX", message)

    def receive_can_frames(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                self.can_message_received.emit("RX", data.decode())
            except:
                break

    @Slot(str, str)
    def update_ui_from_can(self, direction, msg):
        log_entry = f"[{direction}] {msg}\n"
        current_text = self.console.text()
        self.console.setText(log_entry + current_text[:500])

        # Logic to update REB state based on 0x201 (REB_STATUS) [cite: 712, 714, 718]
        if "201" in msg:
            # status_code: 0=IDLE, 1=THEFT_CONFIRMED, 2=BLOCKING, 3=BLOCKED [cite: 718]
            state_map = {"00": "IDLE", "01": "THEFT_CONFIRMED", "02": "BLOCKING", "03": "BLOCKED"}
            try:
                code = msg.split(":")[1][:2]
                state_name = state_map.get(code, "UNKNOWN")
                self.lbl_reb_state.setText(f"REB State: {state_name}")

                # Visual feedback based on state
                if code == "00":
                    self.lbl_reb_state.setStyleSheet("font-size: 16px; font-weight: bold; color: gray;")
                else:
                    self.lbl_reb_state.setStyleSheet("font-size: 16px; font-weight: bold; color: #FF4444;")
            except IndexError:
                pass

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())
