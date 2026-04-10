import socket
import time
import sys
import os

# Adjust path to allow imports from the 'sim' package root
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..')))

from sim.fuel_ecu.fuel_controller import FuelECU

class VehiclePlant:
    """
    Simulates the physical vehicle dynamics (speed, acceleration, friction).
    Sends telemetry messages back to the bus.
    """
    def __init__(self, fuel_ecu):
        self.fuel_ecu = fuel_ecu
        self.bus_addr = ('127.0.0.1', 5000)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))

        self.speed = 0.0  # km/h
        self.max_accel = 5.0  # Max acceleration multiplier
        self.friction = 1.5   # Natural speed decay [cite: 11]

    def run_physics_loop(self):
        print("--- Vehicle Physics Simulation Engine ---")
        while True:
            # Acceleration is limited by the Fuel ECU's power factor [cite: 10]
            effective_accel = self.max_accel * self.fuel_ecu.power_factor

            # Physics formula: v = v_prev + (accel - friction) * dt
            # If stopped, only positive acceleration applies
            if self.speed > 0 or effective_accel > self.friction:
                self.speed += (effective_accel - self.friction) * 0.1

            # Keep speed within realistic bounds
            self.speed = max(0.0, min(180.0, self.speed))

            # Send VEHICLE_STATE telemetry (Simulated ID 0x500) [cite: 11]
            speed_hex = f"{int(self.speed):02x}"
            state_msg = f"500:{speed_hex}000000"
            try:
                self.socket.sendto(state_msg.encode(), self.bus_addr)
            except:
                pass

            # Real-time console status
            status = f"[VEHICLE] Speed: {self.speed:5.1f} km/h | Engine Power: {self.fuel_ecu.power_factor*100:3.0f}%"
            print(status, end='\r')

            time.sleep(0.1) # 10Hz physics update

if __name__ == "__main__":
    # Dependency Injection: VehiclePlant needs FuelECU to check for derating
    f_ecu = FuelECU()
    vehicle = VehiclePlant(f_ecu)
    try:
        vehicle.run_physics_loop()
    except KeyboardInterrupt:
        print("\nPhysics engine stopped.")
