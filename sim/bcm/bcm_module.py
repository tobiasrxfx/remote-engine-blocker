class BCMModule:
    def __init__(self, socket_ref, bus_addr):
        self.socket = socket_ref
        self.bus_addr = bus_addr
        self.door_open = 0
        self.glass_break = 0
        self.horn_active = False
        self.hazard_lights_active = False

    def trigger_intrusion(self):
        # Simula invasão física (0x501)
        # Payload: door(1) glass(1) shock(0) level(3) + padding
        payload = "11030000"
        message = f"501:{payload}"
        try:
            self.socket.sendto(message.encode(), self.bus_addr)
            print(f"[BCM] Sent: {message}")
        except Exception as e:
            print(f"[BCM ERROR] {e}")

    def update_alerts(self, state_code):
        if state_code in ["01", "02"]:
            self.horn_active = True
            self.hazard_lights_active = True
        elif state_code == "03":
            self.horn_active = False
            self.hazard_lights_active = True
        else:
            self.horn_active = False
            self.hazard_lights_active = False