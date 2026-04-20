import sys
import socket
import threading
import os
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
                               QLabel, QPushButton, QHBoxLayout, QFrame, QGroupBox)
from PySide6.QtCore import Qt, Signal, Slot, QTimer

# Configuração de caminhos para importação
root_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
if root_path not in sys.path:
    sys.path.append(root_path)

from sim.bcm.bcm_module import BCMModule
from sim.tcu.tcu_module import TCUModule

class DashboardWindow(QMainWindow):
    can_msg_received = Signal(str, str, str)

    def __init__(self):
        super().__init__()
        self.setWindowTitle("Advanced HMI Simulation - REB System")
        self.resize(1000, 600)
        self.setStyleSheet("QMainWindow { background-color: #121212; }")

        # Comunicação
        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))

        # Módulos
        self.bcm = BCMModule(self.socket, self.bus_addr)
        self.tcu = TCUModule(self.socket, self.bus_addr)

        # Lógica de Animação (Pisca-Alerta)
        self.blink_timer = QTimer()
        self.blink_timer.timeout.connect(self.toggle_blinkers)
        self.blink_state = False

        self.init_ui()

        # Threads e Sinais
        self.can_msg_received.connect(self.update_ui)
        threading.Thread(target=self.receive_can_frames, daemon=True).start()

        self.send_can_raw("000:DASHBOARD_V2_CONNECTED")

    def init_ui(self):
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QHBoxLayout(central_widget) # Divisão Horizontal (Painel vs Celular)

        # ==========================================
        # LADO ESQUERDO: PAINEL DO CARRO (CLUSTER)
        # ==========================================
        cluster_container = QVBoxLayout()

        self.cluster_frame = QFrame()
        self.cluster_frame.setStyleSheet("""
            QFrame {
                background-color: #000000;
                border: 3px solid #333333;
                border-radius: 40px;
            }
        """)
        cluster_inner_layout = QVBoxLayout(self.cluster_frame)

        # Indicadores de Direção (Setas)
        blinkers_layout = QHBoxLayout()
        self.left_arrow = QLabel("⬅")
        self.right_arrow = QLabel("➡")
        arrow_style = "font-size: 50px; color: #111111; font-weight: bold;" # Cor 'apagada' inicial
        self.left_arrow.setStyleSheet(arrow_style)
        self.right_arrow.setStyleSheet(arrow_style)

        blinkers_layout.addWidget(self.left_arrow)
        blinkers_layout.addStretch()
        blinkers_layout.addWidget(self.right_arrow)
        cluster_inner_layout.addLayout(blinkers_layout)

        # Velocímetro Central
        self.lbl_speed = QLabel("0")
        self.lbl_speed.setStyleSheet("font-size: 110px; font-weight: bold; color: #00FF00; border: none;")
        self.lbl_speed.setAlignment(Qt.AlignCenter)
        lbl_unit = QLabel("km/h")
        lbl_unit.setStyleSheet("color: #00FF00; font-size: 20px; border: none;")
        lbl_unit.setAlignment(Qt.AlignCenter)

        cluster_inner_layout.addWidget(self.lbl_speed)
        cluster_inner_layout.addWidget(lbl_unit)

        # Status e Alertas Sonoros Visuais
        self.lbl_reb_status = QLabel("SYSTEM READY")
        self.lbl_reb_status.setStyleSheet("color: #AAAAAA; font-size: 18px; font-weight: bold; border: none;")
        self.lbl_reb_status.setAlignment(Qt.AlignCenter)

        self.lbl_horn_indicator = QLabel("📯 HORN ACTIVE")
        self.lbl_horn_indicator.setStyleSheet("color: #FF0000; font-size: 16px; font-weight: bold; border: none;")
        self.lbl_horn_indicator.hide() # Escondido por padrão

        cluster_inner_layout.addWidget(self.lbl_reb_status)
        cluster_inner_layout.addWidget(self.lbl_horn_indicator)
        cluster_inner_layout.addStretch()

        cluster_container.addWidget(self.cluster_frame)

        # Botão BCM (Simulação Física no Carro)
        self.btn_bcm = QPushButton("💥 SIMULATE IMPACT / GLASS BREAK")
        self.btn_bcm.setStyleSheet("background-color: #444; color: white; padding: 10px; border-radius: 5px;")
        self.btn_bcm.clicked.connect(self.bcm.trigger_intrusion)
        cluster_container.addWidget(self.btn_bcm)

        main_layout.addLayout(cluster_container, 70) # 70% da largura para o carro

        # ==========================================
        # LADO DIREITO: CELULAR (TCU APP)
        # ==========================================
        phone_container = QVBoxLayout()

        self.phone_frame = QFrame()
        self.phone_frame.setFixedWidth(280)
        self.phone_frame.setStyleSheet("""
            QFrame {
                background-color: #222222;
                border: 8px solid #444444;
                border-radius: 30px;
            }
        """)
        phone_layout = QVBoxLayout(self.phone_frame)

        phone_title = QLabel("REB MOBILE APP")
        phone_title.setStyleSheet("color: white; font-weight: bold; font-size: 16px; border: none;")
        phone_title.setAlignment(Qt.AlignCenter)
        phone_layout.addWidget(phone_title)
        phone_layout.addSpacing(40)

        self.btn_remote_block = QPushButton("🔒 REMOTE BLOCK")
        self.btn_remote_block.setFixedHeight(80)
        self.btn_remote_block.setStyleSheet("""
            QPushButton { background-color: #AA0000; color: white; font-weight: bold; border-radius: 15px; border: none; }
            QPushButton:pressed { background-color: #660000; }
        """)
        self.btn_remote_block.clicked.connect(self.tcu.send_remote_block)

        self.btn_remote_unblock = QPushButton("🔓 UNLOCK VEHICLE")
        self.btn_remote_unblock.setFixedHeight(80)
        self.btn_remote_unblock.setStyleSheet("""
            QPushButton { background-color: #0055AA; color: white; font-weight: bold; border-radius: 15px; border: none; }
            QPushButton:pressed { background-color: #003366; }
        """)
        self.btn_remote_unblock.clicked.connect(self.tcu.send_remote_unblock)

        phone_layout.addWidget(self.btn_remote_block)
        phone_layout.addSpacing(20)
        phone_layout.addWidget(self.btn_remote_unblock)
        phone_layout.addStretch()

        phone_container.addWidget(self.phone_frame)
        main_layout.addLayout(phone_container, 30) # 30% da largura para o celular

    def toggle_blinkers(self):
        """Alterna a cor das setas para simular o pisca-alerta."""
        self.blink_state = not self.blink_state
        color = "#00FF00" if self.blink_state else "#111111"
        self.left_arrow.setStyleSheet(f"font-size: 50px; color: {color}; font-weight: bold; border: none;")
        self.right_arrow.setStyleSheet(f"font-size: 50px; color: {color}; font-weight: bold; border: none;")

    def send_can_raw(self, message):
        try:
            self.socket.sendto(message.encode(), self.bus_addr)
        except:
            pass

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
        # Atualiza Velocidade
        if can_id == "500":
            speed = int(payload[:2], 16)
            self.lbl_speed.setText(str(speed))

        # Atualiza Estado do Sistema
        elif can_id == "201":
            state_code = payload[:2]
            mapping = {
                "00": ("SYSTEM READY", "#AAAAAA", False),
                "01": ("THEFT DETECTED", "orange", False),
                "02": ("BLOCKING ACTIVE", "red", True),
                "03": ("VEHICLE BLOCKED", "darkred", True)
            }
            name, color, alarms = mapping.get(state_code, ("UNKNOWN", "white", False))
            self.lbl_reb_status.setText(name)
            self.lbl_reb_status.setStyleSheet(f"color: {color}; font-size: 18px; font-weight: bold; border: none;")

            # Controle de Pisca-Alerta e Buzina
            self.bcm.update_alerts(state_code)

            if alarms and self.bcm.hazard_lights_active:
                if not self.blink_timer.isActive():
                    self.blink_timer.start(500) # Pisca a cada 500ms
            else:
                self.blink_timer.stop()
                self.left_arrow.setStyleSheet("font-size: 50px; color: #111111; font-weight: bold; border: none;")
                self.right_arrow.setStyleSheet("font-size: 50px; color: #111111; font-weight: bold; border: none;")

            if self.bcm.horn_active:
                self.lbl_horn_indicator.show()
            else:
                self.lbl_horn_indicator.hide()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = DashboardWindow()
    window.show()
    sys.exit(app.exec())
