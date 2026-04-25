class BCMModule:
    def __init__(self, socket_ref, bus_addr):
        self.socket = socket_ref
        self.bus_addr = bus_addr
        self.door_open = 0
        self.glass_break = 0
        self.horn_active = False
        self.hazard_lights_active = False

    def trigger_intrusion(self):
        """
        Send BCM_INTRUSION_STATUS (0x501) aligned with the REB DBC:
          byte 0 = door_open
          byte 1 = glass_break
          byte 2 = shock_detected
          byte 3 = intrusion_level
          byte 4..7 = reserved
        """
        door_open = 1
        glass_break = 1
        shock_detected = 0
        intrusion_level = 3

        payload = (
            f"{door_open:02X}"
            f"{glass_break:02X}"
            f"{shock_detected:02X}"
            f"{intrusion_level:02X}"
            "00000000"
        )
        
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