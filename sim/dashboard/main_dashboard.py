import sys
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout, QLabel, QPushButton, QHBoxLayout

class DashboardWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("HMI - REB Virtual Vehicle")
        self.resize(600, 400)

        # Central Widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)

        # REB State
        self.lbl_reb_state = QLabel("REB State: IDLE")
        self.lbl_reb_state.setStyleSheet("font-size: 18px; font-weight: bold;")
        main_layout.addWidget(self.lbl_reb_state)

        # Simple GPS Location
        self.lbl_gps = QLabel("GPS: Longitude -34.8811, Latitude -8.0476 (Approx.)")
        main_layout.addWidget(self.lbl_gps)

        # Vehicle Controls
        controls_layout = QHBoxLayout()

        self.btn_ignition = QPushButton("Turn On Ignition")
        self.btn_ignition.clicked.connect(self.toggle_ignition)
        controls_layout.addWidget(self.btn_ignition)

        self.btn_remote_theft = QPushButton("Simulate Theft (TCU)")
        self.btn_remote_theft.clicked.connect(self.sim_theft)
        controls_layout.addWidget(self.btn_remote_theft)

        main_layout.addLayout(controls_layout)

        # Simplified CAN Log
        self.lbl_log = QLabel("CAN Log: Waiting for bus initialization...")
        self.lbl_log.setStyleSheet("background-color: black; color: lime; padding: 5px;")
        main_layout.addWidget(self.lbl_log)

        self.ignition_on = False

    def toggle_ignition(self):
        self.ignition_on = not self.ignition_on
        status = "ON" if self.ignition_on else "OFF"
        self.btn_ignition.setText("Turn Off Ignition" if self.ignition_on else "Turn On Ignition")
        self.lbl_log.setText(f"CAN Log: [TX] Ignition_Status = {status}")

    def sim_theft(self):
        self.lbl_log.setText("CAN Log: [TX] Remote_Command = THEFT_CONFIRMED")
        self.lbl_reb_state.setText("REB State: THEFT_CONFIRMED")
        self.lbl_reb_state.setStyleSheet("font-size: 18px; font-weight: bold; color: red;")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())
