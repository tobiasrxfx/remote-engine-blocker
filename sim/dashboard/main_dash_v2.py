import sys
import socket
import threading
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                               QLabel, QPushButton, QHBoxLayout, QFrame,
                               QSlider, QCheckBox, QSpinBox)
from PySide6.QtCore import Qt, Signal, Slot

class DashboardV2(QMainWindow):
    can_msg_received = Signal(str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI - Remote Engine Blocker (GUI v2.2)")
        self.resize(1000, 750)

        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))

        self.init_ui()
        self.can_msg_received.connect(self.update_ui)
        threading.Thread(target=self.receive_can_frames, daemon=True).start()
        self.send_can_raw("000:DASHBOARD_CONNECTED")

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # Telemetry Display
        telemetry_layout = QHBoxLayout()
        self.lbl_speed = self.create_display_box(telemetry_layout, "SPEED (KM/H)", "0.0", "#00FF00", 70)
        self.lbl_status = self.create_display_box(telemetry_layout, "REB STATUS", "IDLE", "#AAA", 35)
        main_layout.addLayout(telemetry_layout)

        # Control Panel
        controls_layout = QHBoxLayout()

        # Command Group
        cmd_group = QFrame()
        cmd_group.setFrameShape(QFrame.StyledPanel)
        cmd_vbox = QVBoxLayout(cmd_group)
        cmd_vbox.addWidget(QLabel("COMMAND INPUTS"))

        # Manual and Remote Buttons
        cmd_vbox.addWidget(self.create_btn("REMOTE BLOCK (0x200)", "#800", lambda: self.send_can_raw("200:01000000")))
        cmd_vbox.addWidget(self.create_btn("REMOTE UNBLOCK (0x200)", "#060", lambda: self.send_can_raw("200:02000000")))

        # Updated Panel Logic (ID 502 with methods 02 and 01)
        cmd_vbox.addWidget(self.create_btn("PANEL BLOCK (0x502)", "#A50", lambda: self.send_can_raw("502:0102030000000000")))
        cmd_vbox.addWidget(self.create_btn("PANEL UNLOCK (0x502)", "#44A", lambda: self.send_can_raw("502:0101030000000000")))
        cmd_vbox.addWidget(self.create_btn("PANEL CANCEL (0x503)", "#444", lambda: self.send_can_raw("503:0101030000000000")))
        cmd_vbox.addWidget(self.create_btn("BCM INTRUSION (0x501)", "#750", lambda: self.send_can_raw("501:01000000")))

        # Simulation Group
        sim_group = QFrame()
        sim_group.setFrameShape(QFrame.StyledPanel)
        sim_vbox = QVBoxLayout(sim_group)
        self.speed_slider = QSlider(Qt.Horizontal)
        self.speed_slider.setRange(0, 160)
        self.chk_ign = QCheckBox("Ignition ON")
        self.btn_send = QPushButton("SEND VEHICLE_STATE (0x500)")
        self.btn_send.clicked.connect(self.emit_state)

        sim_vbox.addWidget(QLabel("VEHICLE CONTROL"))
        sim_vbox.addWidget(self.speed_slider)
        sim_vbox.addWidget(self.chk_ign)
        sim_vbox.addWidget(self.btn_send)

        controls_layout.addWidget(cmd_group, 1)
        controls_layout.addWidget(sim_group, 1)
        main_layout.addLayout(controls_layout)

        # Console
        self.console = QLabel("Console Logs...")
        self.console.setStyleSheet("background: black; color: #0F0; font-family: Consolas;")
        self.console.setMinimumHeight(120)
        main_layout.addWidget(self.console)

    def create_display_box(self, layout, title, val, color, size):
        frame = QFrame()
        frame.setFrameShape(QFrame.StyledPanel)
        vbox = QVBoxLayout(frame)
        vbox.addWidget(QLabel(title))
        lbl = QLabel(val)
        lbl.setStyleSheet(f"font-size: {size}px; color: {color}; font-weight: bold;")
        lbl.setAlignment(Qt.AlignCenter)
        vbox.addWidget(lbl)
        layout.addWidget(frame)
        return lbl

    def create_btn(self, text, color, slot):
        btn = QPushButton(text)
        btn.setStyleSheet(f"background: {color}; color: white; font-weight: bold; padding: 5px;")
        btn.clicked.connect(slot)
        return btn

    def send_can_raw(self, msg):
        self.socket.sendto(msg.encode(), self.bus_addr)
        self.log(f"[TX] {msg}")

    def emit_state(self):
        speed = f"{self.speed_slider.value():02X}"
        ign = "01" if self.chk_ign.isChecked() else "00"
        self.send_can_raw(f"500:{speed}00{ign}01000000")

    def log(self, txt):
        lines = self.console.text().split('\n')
        self.console.setText('\n'.join([txt] + lines[:8]))

    def receive_can_frames(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                self.can_msg_received.emit(data.decode(), "")
            except: break

    @Slot(str, str)
    def update_ui(self, msg, _):
        if ":" not in msg: return
        cid, pay = msg.split(":")
        if cid == "500": self.lbl_speed.setText(f"{int(pay[:2], 16)}.0")
        if cid == "201":
            states = {"00": "IDLE", "01": "THEFT_CONFIRMED", "02": "BLOCKING", "03": "BLOCKED"}
            self.lbl_status.setText(states.get(pay[:2], "UNKNOWN"))

if __name__ == "__main__":
    app = QApplication(sys.argv)
    DashboardV2().show()
    sys.exit(app.exec())
