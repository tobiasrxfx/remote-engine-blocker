"""
Integrated presentation dashboard for the REB project.
Keeps the visual layout from test_dashboard.py and integrates the CAN/UDP
behavior from main_dash_v2.py.
"""

import sys
import os
import math
import socket
import struct
import threading

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QFrame,
    QLabel, QPushButton, QSlider, QDial, QGridLayout, QSizePolicy
)
from PySide6.QtCore import Qt, QTimer, QUrl, QRectF, QPointF, Signal, Slot
from PySide6.QtGui import QFont, QPainter, QBrush, QColor, QPen
from PySide6.QtMultimedia import QSoundEffect

root_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
if root_path not in sys.path:
    sys.path.append(root_path)

from sim.bcm.bcm_module import BCMModule
from sim.tcu.tcu_module import TCUModule


class GaugeWidget(QWidget):
    def __init__(self, min_val, max_val, unit, major_ticks):
        super().__init__()
        self.min_val = min_val
        self.max_val = max_val
        self.value = 0
        self.unit = unit
        self.major_ticks = major_ticks
        self.setFixedSize(210, 210)

    def set_value(self, val):
        self.value = max(self.min_val, min(val, self.max_val))
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        width = self.width()
        height = self.height()
        cx = width / 2
        cy = height / 2
        radius = (width / 2) - 15

        painter.setBrush(QBrush(QColor("#050505")))
        painter.setPen(QPen(QColor("#444"), 6))
        painter.drawEllipse(10, 10, width - 20, height - 20)
        painter.translate(cx, cy)

        painter.setPen(QColor("#00FF00"))
        painter.setFont(QFont("Consolas", 24, QFont.Bold))
        painter.drawText(QRectF(-cx, radius * 0.35, width, 30), Qt.AlignCenter, str(int(self.value)))

        painter.setPen(QColor("gray"))
        painter.setFont(QFont("Arial", 9, QFont.Bold))
        unit_str = "RPM x1000" if self.max_val > 1000 and "RPM" in self.unit else self.unit
        painter.drawText(QRectF(-cx, radius * 0.65, width, 20), Qt.AlignCenter, unit_str)

        painter.setPen(QPen(QColor("white"), 2))
        for i in range(self.major_ticks + 1):
            angle = 135 + (i / self.major_ticks) * 270
            rad = math.radians(angle)
            x1 = math.cos(rad) * radius
            y1 = math.sin(rad) * radius
            x2 = math.cos(rad) * (radius - 12)
            y2 = math.sin(rad) * (radius - 12)
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2))
            val = self.min_val + (i / self.major_ticks) * (self.max_val - self.min_val)
            val_str = str(int(val // 1000)) if self.max_val > 1000 else str(int(val))
            x_text = math.cos(rad) * (radius - 28)
            y_text = math.sin(rad) * (radius - 28)
            painter.setFont(QFont("Arial", 9, QFont.Bold))
            painter.drawText(QRectF(x_text - 15, y_text - 10, 30, 20), Qt.AlignCenter, val_str)

        current_angle = 135 + ((self.value - self.min_val) / (self.max_val - self.min_val)) * 270
        rad_n = math.radians(current_angle)
        nx = math.cos(rad_n) * (radius - 16)
        ny = math.sin(rad_n) * (radius - 16)
        painter.setPen(QPen(QColor("#ff2222"), 4))
        painter.drawLine(QPointF(0, 0), QPointF(nx, ny))
        painter.setBrush(QColor("#333"))
        painter.setPen(QPen(QColor("#111"), 2))
        painter.drawEllipse(-8, -8, 16, 16)
        painter.end()


class FrontendPrototype(QMainWindow):
    can_msg_received = Signal(str, str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("REB Presentation Dashboard - Integrated")
        self.resize(1100, 750)
        self.setStyleSheet("background-color: #1e1e1e; color: white;")

        self.bus_addr = ("127.0.0.1", 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))

        self.bcm = BCMModule(self.socket, self.bus_addr)
        self.tcu = TCUModule(self.socket, self.bus_addr)

        self.ignition_state = 0
        self.hazards_active = False
        self.horn_active = False
        self.arrows_visible = False
        self.sim_time_ms = 1000
        self.last_trigger_source = "NONE"
        self.next_panel_nonce = 1

        self.last_derate_percent = 0
        self.last_starter_lock = False
        self.last_tcu_status_tx = False
        self.engine_ecu_locked = False

        self.init_audio()
        self.init_ui()

        self.can_msg_received.connect(self.update_ui)
        self.listen_thread = threading.Thread(target=self.receive_can_frames, daemon=True)
        self.listen_thread.start()

        self.hazard_timer = QTimer()
        self.hazard_timer.timeout.connect(self.blink_hazards)

        self.demo_steps = []
        self.demo_index = 0

        self.demo_timer = QTimer()
        self.demo_timer.timeout.connect(self.run_next_demo_step)

        self.send_can_raw("000:DASHBOARD_CONNECTED")

    def init_audio(self):
        base_dir = os.path.dirname(os.path.abspath(__file__))
        self.horn_sound = QSoundEffect()
        self.horn_sound.setSource(QUrl.fromLocalFile(os.path.join(base_dir, "horn.wav")))
        self.engine_sound = QSoundEffect()
        self.engine_sound.setSource(QUrl.fromLocalFile(os.path.join(base_dir, "engine_start.wav")))

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setSpacing(10)
        main_layout.setContentsMargins(10, 10, 10, 10)

        top_layout = QHBoxLayout()
        top_layout.setSpacing(10)

        car_panel = QFrame()
        car_panel.setStyleSheet("background-color: #121212; border-radius: 10px; border: 2px solid #333;")
        car_layout = QVBoxLayout(car_panel)
        car_layout.setContentsMargins(15, 15, 15, 15)
        car_layout.setSpacing(10)

        alerts_layout = QHBoxLayout()
        self.left_arrow = QLabel("⬅\uFE0E")
        self.right_arrow = QLabel("➡\uFE0E")
        for arrow in [self.left_arrow, self.right_arrow]:
            arrow.setFont(QFont("Segoe UI Symbol", 42, QFont.Bold))
            arrow.setStyleSheet("color: #222222; border: none;")
            arrow.setAlignment(Qt.AlignCenter)
        alerts_layout.addWidget(self.left_arrow)
        alerts_layout.addStretch()
        alerts_layout.addWidget(self.right_arrow)
        car_layout.addLayout(alerts_layout)

        dials_widget = QWidget()
        dials_layout = QHBoxLayout(dials_widget)
        dials_layout.setContentsMargins(0, 0, 0, 0)
        dials_layout.setSpacing(20)

        self.lbl_speed = GaugeWidget(min_val=0, max_val=220, unit="km/h", major_ticks=11)
        self.lbl_rpm = GaugeWidget(min_val=0, max_val=8000, unit="RPM", major_ticks=8)

        throttle_frame = QFrame()
        throttle_frame.setFixedWidth(100)
        throttle_frame.setStyleSheet("background-color: transparent; border: none;")
        throttle_layout = QVBoxLayout(throttle_frame)
        throttle_layout.setContentsMargins(0, 0, 0, 0)

        self.throttle_slider = QSlider(Qt.Vertical)
        self.throttle_slider.setRange(0, 100)
        self.throttle_slider.setValue(0)
        self.throttle_slider.setStyleSheet("""
            QSlider::groove:vertical { background: #0a0a0a; border: 2px inset #222; width: 16px; border-radius: 8px; margin: 0px 0px; }
            QSlider::handle:vertical { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #888, stop:0.5 #ddd, stop:1 #666); border: 2px solid #111; height: 40px; margin: 0px -30px; border-radius: 6px; }
            QSlider::handle:vertical:pressed { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #666, stop:0.5 #bbb, stop:1 #444); }
        """)
        self.throttle_slider.valueChanged.connect(self.update_throttle)

        lbl_throttle = QLabel("THROTTLE")
        lbl_throttle.setAlignment(Qt.AlignCenter)
        lbl_throttle.setStyleSheet("color: gray; font-size: 11px; font-weight: bold; border: none; margin-top: 5px;")
        throttle_layout.addWidget(self.throttle_slider, alignment=Qt.AlignHCenter)
        throttle_layout.addWidget(lbl_throttle)

        dials_layout.addStretch()
        dials_layout.addWidget(self.lbl_speed, alignment=Qt.AlignCenter)
        dials_layout.addWidget(throttle_frame, alignment=Qt.AlignCenter)
        dials_layout.addWidget(self.lbl_rpm, alignment=Qt.AlignCenter)
        dials_layout.addStretch()
        car_layout.addWidget(dials_widget, stretch=5)

        dashboard_bottom_layout = QHBoxLayout()
        dashboard_bottom_layout.setSpacing(15)

        ign_frame = QFrame()
        ign_frame.setFixedWidth(100)
        ign_frame.setStyleSheet("background-color: #2a2a2a; border-radius: 8px; border: 1px solid #444;")
        ign_layout = QVBoxLayout(ign_frame)
        ign_layout.setContentsMargins(5, 5, 5, 5)
        ign_title = QLabel("IGNITION")
        ign_title.setAlignment(Qt.AlignCenter)
        ign_title.setStyleSheet("color: gray; font-size: 10px; font-weight: bold; border: none;")
        ign_layout.addWidget(ign_title)

        self.ign_dial = QDial()
        self.ign_dial.setRange(0, 2)
        self.ign_dial.setNotchesVisible(True)
        self.ign_dial.setFixedSize(50, 50)
        self.ign_dial.setStyleSheet("background-color: #555; border-radius: 25px;")
        self.ign_dial.valueChanged.connect(self.update_ignition_state)
        ign_layout.addWidget(self.ign_dial, alignment=Qt.AlignCenter)

        self.lbl_ign_state = QLabel("OFF")
        self.lbl_ign_state.setStyleSheet("color: gray; font-weight: bold; font-size: 11px; border: none;")
        self.lbl_ign_state.setAlignment(Qt.AlignCenter)
        ign_layout.addWidget(self.lbl_ign_state)
        dashboard_bottom_layout.addWidget(ign_frame)

        buttons_frame = QFrame()
        buttons_frame.setStyleSheet("background-color: #111; border-radius: 8px; border: 1px solid #222;")
        buttons_layout = QGridLayout(buttons_frame)
        buttons_layout.setContentsMargins(10, 10, 10, 10)
        buttons_layout.setSpacing(8)

        self.btn_panel_block = self.create_car_button("PANEL\nBLOCK")
        self.btn_panel_unlock = self.create_car_button("PANEL\nAUTH")
        self.btn_panel_cancel = self.create_car_button("PANEL\nCANCEL")
        self.btn_bcm_intrusion = self.create_car_button("BCM\nINTRUSION")
        self.btn_test_hazard = self.create_car_button("HAZARDS\n(Test)")
        self.btn_test_horn = self.create_car_button("HORN\n(Test)")
        self.btn_demo_stolen_vehicle = self.create_app_button("DEMO STOLEN VEHICLE", "#AA0000", "4px", "white")
        self.btn_demo_stolen_vehicle.clicked.connect(self.start_stolen_vehicle_demo)
        

        self.btn_panel_block.clicked.connect(self.send_panel_block)
        self.btn_panel_unlock.clicked.connect(self.send_panel_auth)
        self.btn_panel_cancel.clicked.connect(self.send_panel_cancel)
        self.btn_bcm_intrusion.clicked.connect(self.send_bcm_intrusion)
        self.btn_test_hazard.clicked.connect(self.toggle_hazards)
        self.btn_test_horn.clicked.connect(self.toggle_horn)

        buttons_layout.addWidget(self.btn_panel_block, 0, 0)
        buttons_layout.addWidget(self.btn_panel_unlock, 0, 1)
        buttons_layout.addWidget(self.btn_panel_cancel, 0, 2)
        buttons_layout.addWidget(self.btn_bcm_intrusion, 1, 0)
        buttons_layout.addWidget(self.btn_test_hazard, 1, 1)
        buttons_layout.addWidget(self.btn_test_horn, 1, 2)
        dashboard_bottom_layout.addWidget(buttons_frame, stretch=1)
        car_layout.addLayout(dashboard_bottom_layout, stretch=2)
        top_layout.addWidget(car_panel, stretch=7)

        smartphone_container = QFrame()
        smartphone_container.setObjectName("phone_body")
        smartphone_container.setMaximumWidth(280)
        smartphone_container.setMinimumWidth(260)
        smartphone_container.setStyleSheet("""
            QFrame#phone_body { background-color: #222224; border: 2px solid #333335; border-radius: 35px; }
        """)
        phone_body_layout = QVBoxLayout(smartphone_container)
        phone_body_layout.setContentsMargins(12, 30, 12, 20)
        phone_body_layout.setSpacing(15)
        speaker_slit = QFrame()
        speaker_slit.setFixedSize(8, 8)
        speaker_slit.setStyleSheet("background-color: #444444; border-radius: 4px;")
        phone_body_layout.addWidget(speaker_slit, alignment=Qt.AlignHCenter)

        phone_screen = QFrame()
        phone_screen.setObjectName("phone_screen")
        phone_screen.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        phone_screen.setStyleSheet("""
            QFrame#phone_screen { background-color: #111115; border: 1px solid #222; border-radius: 2px; }
        """)
        screen_layout = QVBoxLayout(phone_screen)
        screen_layout.setContentsMargins(20, 30, 20, 30)
        phone_title = QLabel("REB App")
        phone_title.setStyleSheet("color: white; font-size: 16px; font-weight: bold; border: none;")
        phone_title.setAlignment(Qt.AlignCenter)
        screen_layout.addWidget(phone_title)
        self.phone_status = QLabel("ESTADO: OPERACIONAL")
        self.phone_status.setStyleSheet("color: white; font-size: 12px; font-weight: bold; border: none;")
        self.phone_status.setAlignment(Qt.AlignCenter)
        screen_layout.addWidget(self.phone_status)
        screen_layout.addStretch()

        self.btn_remote_block = QPushButton("Remote Block")
        self.btn_remote_block.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.btn_remote_block.setMinimumHeight(45)
        self.btn_remote_block.setStyleSheet("""
            QPushButton { background-color: #abc2f4; color: #111; font-weight: bold; font-size: 14px; border-radius: 8px; border: none; }
            QPushButton:pressed { background-color: #8ab4f8; }
        """)
        self.btn_remote_block.clicked.connect(self.send_remote_block)

        self.btn_remote_unblock = QPushButton("Remote Unblock")
        self.btn_remote_unblock.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.btn_remote_unblock.setMinimumHeight(45)
        self.btn_remote_unblock.setStyleSheet("""
            QPushButton { background-color: #2c2c30; color: white; font-weight: bold; font-size: 14px; border-radius: 8px; border: 1px solid #555; }
            QPushButton:pressed { background-color: #1e1e22; }
        """)
        self.btn_remote_unblock.clicked.connect(self.send_remote_unblock)
        screen_layout.addWidget(self.btn_remote_block)
        screen_layout.addWidget(self.btn_remote_unblock)
        screen_layout.addStretch()
        phone_body_layout.addWidget(phone_screen)
        home_button = QPushButton()
        home_button.setFixedSize(48, 48)
        home_button.setText("◻")
        home_button.setStyleSheet("""
            QPushButton { background-color: transparent; color: #555555; font-size: 26px; border: 2px solid #555555; border-radius: 24px; padding-bottom: 4px; }
            QPushButton:pressed { background-color: #111111; }
        """)
        phone_body_layout.addWidget(home_button, alignment=Qt.AlignHCenter)
        top_layout.addWidget(smartphone_container, stretch=3)
        main_layout.addLayout(top_layout, stretch=6)

        reb_module_layout = QHBoxLayout()
        reb_module_layout.setSpacing(10)

        reb_status_frame = QFrame()
        reb_status_frame.setStyleSheet("background-color: #1a1a1a; border: 1px solid #444; border-radius: 5px;")
        reb_status_layout = QVBoxLayout(reb_status_frame)
        reb_status_layout.setContentsMargins(5, 5, 5, 5)
        lbl_st_title = QLabel("REB SYSTEM STATUS")
        lbl_st_title.setStyleSheet("font-size: 11px;")
        reb_status_layout.addWidget(lbl_st_title)
        self.lbl_reb_state = QLabel("IDLE")
        self.lbl_reb_state.setStyleSheet("font-size: 24px; font-weight: bold; color: gray; border: none;")
        self.lbl_reb_state.setAlignment(Qt.AlignCenter)
        reb_status_layout.addWidget(self.lbl_reb_state)
        self.lbl_alerts = QLabel("ALERTS: OFF")
        self.lbl_alerts.setStyleSheet("font-size: 12px; color: gray; border: none;")
        self.lbl_alerts.setAlignment(Qt.AlignCenter)
        reb_status_layout.addWidget(self.lbl_alerts)
        self.btn_send_vehicle = QPushButton("SEND 0x500")
        self.btn_send_vehicle.setStyleSheet("background-color: #005555; color: white; font-weight: bold; padding: 6px; border-radius: 4px;")
        self.btn_send_vehicle.clicked.connect(self.send_vehicle_state)
        reb_status_layout.addWidget(self.btn_send_vehicle)
        reb_module_layout.addWidget(reb_status_frame, stretch=1)

        actuation_frame = QFrame()
        actuation_frame.setStyleSheet("background-color: #1a1a1a; border: 1px solid #444; border-radius: 5px;")
        actuation_layout = QVBoxLayout(actuation_frame)
        actuation_layout.setContentsMargins(5, 5, 5, 5)
        lbl_ecu_title = QLabel("ACTUATION / ECU")
        lbl_ecu_title.setStyleSheet("font-size: 11px;")
        actuation_layout.addWidget(lbl_ecu_title)
        self.lbl_starter_lock = QLabel("Starter Lock: OFF")
        self.lbl_starter_lock.setStyleSheet("font-size: 12px; color: gray; border: none;")
        actuation_layout.addWidget(self.lbl_starter_lock)
        self.lbl_derate = QLabel("Derate: 0%")
        self.lbl_derate.setStyleSheet("font-size: 12px; color: gray; border: none;")
        actuation_layout.addWidget(self.lbl_derate)
        self.lbl_tcu_tx = QLabel("TCU Status TX: NO")
        self.lbl_tcu_tx.setStyleSheet("font-size: 12px; color: gray; border: none;")
        actuation_layout.addWidget(self.lbl_tcu_tx)
        self.lbl_engine_ecu = QLabel("Engine ECU: UNLOCKED")
        self.lbl_engine_ecu.setStyleSheet("font-size: 12px; color: gray; border: none;")
        actuation_layout.addWidget(self.lbl_engine_ecu)
        reb_module_layout.addWidget(actuation_frame, stretch=1)

        time_frame = QFrame()
        time_frame.setStyleSheet("background-color: #1a1a1a; border: 1px solid #444; border-radius: 5px;")
        time_layout = QVBoxLayout(time_frame)
        time_layout.setContentsMargins(5, 5, 5, 5)
        lbl_time_title = QLabel("TIME CONTROL")
        lbl_time_title.setStyleSheet("font-size: 11px;")
        time_layout.addWidget(lbl_time_title)
        self.lbl_sim_time = QLabel(f"{self.sim_time_ms} ms")
        self.lbl_sim_time.setStyleSheet("font-size: 20px; font-weight: bold; color: #00AAFF; border: none;")
        self.lbl_sim_time.setAlignment(Qt.AlignCenter)
        time_layout.addWidget(self.lbl_sim_time)

        time_layout.addWidget(self.btn_demo_stolen_vehicle)

        time_grid = QGridLayout()
        time_grid.setSpacing(4)
        self.btn_tick_1s = self.create_app_button("+1s", "#333", "4px")
        self.btn_tick_10s = self.create_app_button("+10s", "#333", "4px")
        self.btn_tick_30s = self.create_app_button("+30s", "#333", "4px")
        self.btn_tick_60s = self.create_app_button("+60s", "#333", "4px")
        self.btn_tick_120s = self.create_app_button("+120s", "#333", "4px")
        self.btn_tick_1s.clicked.connect(lambda: self.send_time_tick(1000))
        self.btn_tick_10s.clicked.connect(lambda: self.send_time_tick(10000))
        self.btn_tick_30s.clicked.connect(lambda: self.send_time_tick(30000))
        self.btn_tick_60s.clicked.connect(lambda: self.send_time_tick(60000))
        self.btn_tick_120s.clicked.connect(lambda: self.send_time_tick(120000))
        time_grid.addWidget(self.btn_tick_1s, 0, 0)
        time_grid.addWidget(self.btn_tick_10s, 0, 1)
        time_grid.addWidget(self.btn_tick_30s, 0, 2)
        time_grid.addWidget(self.btn_tick_60s, 1, 0)
        time_grid.addWidget(self.btn_tick_120s, 1, 1, 1, 2)
        time_layout.addLayout(time_grid)

        jump_layout = QHBoxLayout()
        jump_layout.setSpacing(4)
        self.btn_go_blocking = self.create_app_button("BLOCKING", "#abc2f4", "4px", "#111111")
        self.btn_go_blocked = self.create_app_button("BLOCKED", "#abc2f4", "4px", "#111111")
        self.btn_go_blocking.clicked.connect(self.jump_to_blocking)
        self.btn_go_blocked.clicked.connect(self.jump_to_blocked)
        jump_layout.addWidget(self.btn_go_blocking)
        jump_layout.addWidget(self.btn_go_blocked)
        time_layout.addLayout(jump_layout)
        reb_module_layout.addWidget(time_frame, stretch=2)
        main_layout.addLayout(reb_module_layout, stretch=2)

        self.console = QLabel("[DASHBOARD_CONNECTED]\nAguardando telemetria...")
        self.console.setStyleSheet("background-color: black; color: #00FF00; font-family: Consolas; padding: 5px; border: 1px solid #333; border-radius: 4px; font-size: 12px;")
        self.console.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        self.console.setWordWrap(True)
        self.console.setMaximumHeight(80)
        self.console.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        main_layout.addWidget(self.console, stretch=1)

    def create_car_button(self, text):
        btn = QPushButton(text)
        btn.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        btn.setMinimumHeight(45)
        btn.setStyleSheet("""
            QPushButton { background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4a4a, stop:1 #2b2b2b); color: #dddddd; font-weight: bold; font-size: 10px; border: 2px solid #151515; border-radius: 6px; }
            QPushButton:pressed { background-color: #111111; color: #ffffff; border: 2px solid #000000; }
        """)
        return btn

    def create_app_button(self, text, color, radius="8px", text_color="white"):
        btn = QPushButton(text)
        btn.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        btn.setMinimumHeight(35)
        btn.setStyleSheet(f"""
            QPushButton {{ background-color: {color}; color: {text_color}; font-weight: bold; font-size: 12px; padding: 4px; border-radius: {radius}; border: none; }}
            QPushButton:pressed {{ background-color: #222222; color: white; }}
        """)
        return btn

    def send_can_raw(self, message):
        try:
            self.socket.sendto(message.encode(), self.bus_addr)
            print(f"[DASHBOARD] Sent: {message}")
            self.log_console(f"[TX] {message}")
        except Exception as e:
            print(f"[DASHBOARD ERROR] {e}")

    def _u16le_hex(self, value: int) -> str:
        return struct.pack("<H", value).hex().upper()

    def _next_panel_nonce(self) -> int:
        value = self.next_panel_nonce
        self.next_panel_nonce += 1
        return value

    def update_throttle(self, value):
        if self.ignition_state == 2:
            speed = int((value / 100.0) * 220)
            rpm = 800 + int((value / 100.0) * 7200)
        else:
            speed = 0
            rpm = 0
        self.lbl_speed.set_value(speed)
        self.lbl_rpm.set_value(rpm)
        self.send_vehicle_state()

    def update_ignition_state(self, value):
        self.ignition_state = value
        if value == 0:
            self.lbl_ign_state.setText("OFF")
            self.lbl_ign_state.setStyleSheet("color: gray; font-weight: bold; font-size: 11px; border: none;")
            self.throttle_slider.setValue(0)
            self.update_throttle(0)
        elif value == 1:
            self.lbl_ign_state.setText("ON")
            self.lbl_ign_state.setStyleSheet("color: #00AAFF; font-weight: bold; font-size: 11px; border: none;")
            self.update_throttle(self.throttle_slider.value())
        elif value == 2:
            self.lbl_ign_state.setText("START")
            self.lbl_ign_state.setStyleSheet("color: #00FF00; font-weight: bold; font-size: 11px; border: none;")
            self.update_throttle(self.throttle_slider.value())
            if not self.engine_sound.isPlaying():
                self.engine_sound.play()

    def send_remote_block(self):
        self.last_trigger_source = "REMOTE"
        self.tcu.send_remote_block()
        self.log_console("[TX] REB_CMD Remote Block (0x200)")

    def send_remote_unblock(self):
        self.last_trigger_source = "UNLOCK"
        self.tcu.send_remote_unblock()
        self.log_console("[TX] REB_CMD Remote Unblock (0x200)")

    def send_panel_auth(self):
        self.last_trigger_source = "UNLOCK"
        auth_request = 1
        auth_method = 1
        auth_nonce = self._next_panel_nonce()
        payload = f"{auth_request:02X}{auth_method:02X}{self._u16le_hex(auth_nonce)}00000000"
        self.send_can_raw(f"502:{payload}")

    def send_panel_cancel(self):
        self.last_trigger_source = "UNLOCK"
        cancel_request = 1
        cancel_reason = 3
        cancel_nonce = self._next_panel_nonce()
        payload = f"{cancel_request:02X}{cancel_reason:02X}{self._u16le_hex(cancel_nonce)}00000000"
        self.send_can_raw(f"503:{payload}")

    def send_bcm_intrusion(self):
        self.last_trigger_source = "BCM"
        self.bcm.trigger_intrusion()
        self.log_console("[TX] BCM_INTRUSION_STATUS (0x501)")

    def send_panel_block(self):
        self.last_trigger_source = "PANEL"
        block_request = 1
        auth_method = 1
        block_nonce = self._next_panel_nonce()
        payload = f"{block_request:02X}{auth_method:02X}{self._u16le_hex(block_nonce)}00000000"
        self.send_can_raw(f"504:{payload}")

    def send_vehicle_state(self):
        speed_kmh = int(self.lbl_speed.value)
        speed_centi = int(speed_kmh * 100)
        ignition_on = 1 if self.ignition_state >= 1 else 0
        engine_running = 1 if self.ignition_state == 2 else 0
        rpm = int(self.lbl_rpm.value)
        payload = f"{self._u16le_hex(speed_centi)}{ignition_on:02X}{engine_running:02X}{self._u16le_hex(rpm)}0000"
        self.send_can_raw(f"500:{payload}")

    def send_time_tick(self, delta_ms):
        self.sim_time_ms += delta_ms
        self.lbl_sim_time.setText(f"{self.sim_time_ms} ms")
        payload = bytearray(8)
        payload[0] = 0x02
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
            self.lbl_starter_lock.setStyleSheet("font-size: 12px; color: red; font-weight: bold; border: none;")
        else:
            self.lbl_starter_lock.setText("Starter Lock: OFF")
            self.lbl_starter_lock.setStyleSheet("font-size: 12px; color: gray; border: none;")

    def update_derate_ui(self, derate_percent: int):
        self.last_derate_percent = derate_percent
        if derate_percent > 0:
            self.lbl_derate.setText(f"Derate: {derate_percent}%")
            self.lbl_derate.setStyleSheet("font-size: 12px; color: orange; font-weight: bold; border: none;")
        else:
            self.lbl_derate.setText("Derate: 0%")
            self.lbl_derate.setStyleSheet("font-size: 12px; color: gray; border: none;")

    def update_tcu_tx_ui(self, active: bool):
        self.last_tcu_status_tx = active
        if active:
            self.lbl_tcu_tx.setText("TCU Status TX: YES")
            self.lbl_tcu_tx.setStyleSheet("font-size: 12px; color: #00AAFF; font-weight: bold; border: none;")
        else:
            self.lbl_tcu_tx.setText("TCU Status TX: NO")
            self.lbl_tcu_tx.setStyleSheet("font-size: 12px; color: gray; border: none;")

    def update_engine_ecu_ui(self, locked: bool):
        self.engine_ecu_locked = locked
        if locked:
            self.lbl_engine_ecu.setText("Engine ECU: LOCKED")
            self.lbl_engine_ecu.setStyleSheet("font-size: 12px; color: darkred; font-weight: bold; border: none;")
        else:
            self.lbl_engine_ecu.setText("Engine ECU: UNLOCKED")
            self.lbl_engine_ecu.setStyleSheet("font-size: 12px; color: gray; border: none;")

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
        self.log_console(f"[RX] {full_msg}")

        if can_id == "500":
            try:
                speed_centi_kmh = int.from_bytes(bytes.fromhex(payload[:4]), "little")
                speed_kmh = speed_centi_kmh / 100.0
                self.lbl_speed.set_value(speed_kmh)
                if len(payload) >= 12:
                    rpm = int.from_bytes(bytes.fromhex(payload[8:12]), "little")
                    self.lbl_rpm.set_value(rpm)
            except Exception:
                pass

        elif can_id == "201":
            state_code = payload[:2]
            blocked_flag_hex = payload[2:4]
            mapping = {
                "00": ("IDLE", "gray"),
                "01": ("THEFT_CONFIRMED", "orange"),
                "02": ("BLOCKING", "red"),
                "03": ("BLOCKED", "darkred"),
            }
            name, color = mapping.get(state_code, ("UNKNOWN", "white"))
            self.lbl_reb_state.setText(name)
            self.lbl_reb_state.setStyleSheet(f"font-size: 24px; font-weight: bold; color: {color}; border: none;")
            self.phone_status.setText(f"ESTADO: {name}")
            self.bcm.update_alerts(state_code)
            self.update_alerts_from_reb_state(state_code)
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

    def update_alerts_from_reb_state(self, state_code: str):
        """
        Automatic horn/hazard behavior follows the trigger source.

        Requirements-aligned rule used for the demo:
        - BCM intrusion: horn + hazards may be activated in THEFT_CONFIRMED/BLOCKING.
        - Panel block: no horn/hazards, because it is an intentional local action.
        - Remote block: no horn/hazards, only system status changes.
        - Auth/unlock/IDLE: alerts are cleared.
        """
        alarm_source = self.last_trigger_source == "BCM"
        alert_active = alarm_source and state_code in ("01", "02")

        if alert_active:
            if not self.hazards_active:
                self.hazards_active = True
                self.hazard_timer.start(500)

            if not self.horn_active:
                self.horn_active = True
                self.horn_sound.setLoopCount(-2)
                self.horn_sound.play()
        else:
            self.hazards_active = False
            self.horn_active = False
            self.hazard_timer.stop()
            self.horn_sound.stop()
            self.arrows_visible = False
            self.left_arrow.setStyleSheet("color: #222222; border: none;")
            self.right_arrow.setStyleSheet("color: #222222; border: none;")

        self.update_alert_label()

    def toggle_hazards(self):
        self.hazards_active = not self.hazards_active
        if self.hazards_active:
            self.hazard_timer.start(500)
            self.btn_test_hazard.setText("HAZARDS\n(ON)")
            self.log_console("[TEST] HAZARDS ON")
        else:
            self.hazard_timer.stop()
            self.arrows_visible = False
            self.left_arrow.setStyleSheet("color: #222222; border: none;")
            self.right_arrow.setStyleSheet("color: #222222; border: none;")
            self.btn_test_hazard.setText("HAZARDS\n(Test)")
            self.log_console("[TEST] HAZARDS OFF")
        self.update_alert_label()

    def blink_hazards(self):
        self.arrows_visible = not self.arrows_visible
        color = "#00FF00" if self.arrows_visible else "#222222"
        self.left_arrow.setStyleSheet(f"color: {color}; border: none;")
        self.right_arrow.setStyleSheet(f"color: {color}; border: none;")

    def toggle_horn(self):
        self.horn_active = not self.horn_active
        if self.horn_active:
            self.horn_sound.setLoopCount(-2)
            self.horn_sound.play()
            self.btn_test_horn.setText("HORN\n(ON)")
            self.log_console("[TEST] HORN ON")
        else:
            self.horn_sound.stop()
            self.horn_sound.setLoopCount(1)
            self.btn_test_horn.setText("HORN\n(Test)")
            self.log_console("[TEST] HORN OFF")
        self.update_alert_label()

    def update_alert_label(self):
        alerts = []
        if self.horn_active:
            alerts.append("HORN")
        if self.hazards_active:
            alerts.append("HAZARDS")
        if alerts:
            self.lbl_alerts.setText(f"ALERTS: {' & '.join(alerts)} ACTIVE!")
            self.lbl_alerts.setStyleSheet("font-size: 12px; color: orange; font-weight: bold; border: none;")
        else:
            self.lbl_alerts.setText("ALERTS: OFF")
            self.lbl_alerts.setStyleSheet("font-size: 12px; color: gray; border: none;")

    def log_console(self, text):
        current_lines = self.console.text().split("\n")[:3]
        self.console.setText(f"{text}\n" + "\n".join(current_lines))

    def set_vehicle_state(self, speed_kmh: int, rpm: int = 2000):
        self.lbl_speed.set_value(speed_kmh)
        self.lbl_rpm.set_value(rpm)

        speed_centi = int(speed_kmh * 100)

        ignition_on = 1
        engine_running = 1 if rpm > 0 else 0

        payload = (
            f"{self._u16le_hex(speed_centi)}"
            f"{ignition_on:02X}"
            f"{engine_running:02X}"
            f"{self._u16le_hex(rpm)}"
            "0000"
        )

        self.send_can_raw(f"500:{payload}")

    def stop_vehicle_engine(self):
        self.ign_dial.setValue(0)
        self.lbl_speed.set_value(0)
        self.lbl_rpm.set_value(0)

        speed_centi = 0
        ignition_on = 0
        engine_running = 0
        rpm = 0

        payload = (
            f"{self._u16le_hex(speed_centi)}"
            f"{ignition_on:02X}"
            f"{engine_running:02X}"
            f"{self._u16le_hex(rpm)}"
            "0000"
        )

        self.send_can_raw(f"500:{payload}")

    def start_stolen_vehicle_demo(self):
        self.log_console("[DEMO] Starting stolen vehicle scenario")

        self.demo_steps = [
            lambda: self.ign_dial.setValue(1),  # ignition ON
            lambda: self.ign_dial.setValue(2),  # engine START / running

            lambda: self.set_vehicle_state(50, 2500),
            lambda: self.send_remote_block(),

            # Wait theft confirmation window / move to blocking
            lambda: self.send_time_tick(60000),
            lambda: self.set_vehicle_state(45, 2300),

            lambda: self.send_time_tick(10000),
            lambda: self.set_vehicle_state(35, 2000),

            lambda: self.send_time_tick(10000),
            lambda: self.set_vehicle_state(25, 1700),

            lambda: self.send_time_tick(10000),
            lambda: self.set_vehicle_state(15, 1300),

            lambda: self.send_time_tick(10000),
            lambda: self.set_vehicle_state(5, 1000),

            lambda: self.send_time_tick(10000),
            lambda: self.set_vehicle_state(0, 900),

            lambda: self.stop_vehicle_engine(),
            
            # Allow transition to BLOCKED after vehicle is stopped
            lambda: self.send_time_tick(10000),
            lambda: self.send_time_tick(10000),

        ]

        self.demo_index = 0
        self.demo_timer.start(1200)


    def run_next_demo_step(self):
        if self.demo_index >= len(self.demo_steps):
            self.demo_timer.stop()
            self.log_console("[DEMO] Stolen vehicle scenario finished")
            return

        step = self.demo_steps[self.demo_index]
        step()
        self.demo_index += 1


if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = FrontendPrototype()
    window.show()
    sys.exit(app.exec())
