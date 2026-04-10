import socket
import threading

class FuelECU:
    def __init__(self):
        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0)) # Porta aleatória

        self.power_factor = 1.0  # 1.0 = 100% de potência, 0.0 = Motor desligado

        # Registrar no barramento
        self.socket.sendto(b"000:FUEL_ECU_CONNECTED", self.bus_addr)

        # Thread para escutar o barramento
        threading.Thread(target=self.receive_can, daemon=True).start()

    def receive_can(self):
        while True:
            try:
                data, _ = self.socket.recvfrom(1024)
                msg = data.decode()

                # REB_DERATE_CMD (0x400) -> Formato "400:XX..." onde XX é derate_pct em hex
                if msg.startswith("400:"):
                    # Pega os primeiros 2 caracteres após o : (derate_pct)
                    derate_hex = msg.split(":")[1][:2]
                    derate_pct = int(derate_hex, 16)

                    # Calcula o fator de potência (ex: 30% derate -> 0.7 power)
                    self.power_factor = (100 - derate_pct) / 100.0
                    print(f"[FUEL_ECU] Power limited to: {self.power_factor*100:.1f}%")

            except Exception as e:
                print(f"[FUEL_ECU] Error: {e}")
                break

if __name__ == "__main__":
    ecu = FuelECU()
    print("Fuel ECU Active - Waiting for 0x400...")
    import time
    while True: time.sleep(1)
