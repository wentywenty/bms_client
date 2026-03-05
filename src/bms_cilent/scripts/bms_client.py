#!/usr/bin/env python3
import socket
import struct
import os
import time

class BmsClient:
    """
    BMS Daemon Python Client Interface
    Used to read battery data via Unix Domain Socket
    """
    # Struct format definition (matches #pragma pack(push, 1) in C++)
    # d: double (8 bytes), I: uint32 (4 bytes), H: uint16 (2 bytes), 33s: char[33]
    # Total length: 121 bytes
    STRUCT_FORMAT = "<dddddddIHdd33sHHHI"
    STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)

    def __init__(self, socket_path="/tmp/bms.sock"):
        self.socket_path = socket_path
        self.sock = None

    def connect(self):
        """Attempt to connect to the Daemon"""
        try:
            if self.sock:
                self.close()
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(self.socket_path)
            self.sock.settimeout(2.0)
            return True
        except Exception as e:
            self.sock = None
            return False

    def close(self):
        """Close the connection"""
        if self.sock:
            self.sock.close()
            self.sock = None

    def read_data(self):
        """
        Read and parse data once
        Returns a dict format result, or None if failed
        """
        if not self.sock:
            if not self.connect():
                return None

        try:
            data = self.sock.recv(self.STRUCT_SIZE)
            if len(data) < self.STRUCT_SIZE:
                self.close()
                return None

            # Parse binary data
            unpacked = struct.unpack(self.STRUCT_FORMAT, data)
            
            # Map to dictionary
            status = {
                "voltage": unpacked[0],
                "current": unpacked[1],
                "temperature": unpacked[2],
                "percentage": unpacked[3],
                "charge": unpacked[4],
                "capacity": unpacked[5],
                "design_capacity": unpacked[6],
                "protect_status": unpacked[7],
                "work_state": unpacked[8],
                "max_cell_voltage": unpacked[9],
                "min_cell_voltage": unpacked[10],
                "serial_number": unpacked[11].decode('ascii').strip('\x00'),
                "sw_version": unpacked[12],
                "hw_version": unpacked[13],
                "soh": unpacked[14],
                "cycles": unpacked[15]
            }
            return status
        except Exception as e:
            self.close()
            return None

if __name__ == "__main__":
    # --- Simple test run logic ---
    print("=== BMS Python Client Test ===")
    client = BmsClient()
    
    while True:
        data = client.read_data()
        if data:
            print(f"\r[Data] Volts: {data['voltage']:.2f}V | Amps: {data['current']:.2f}A | "
                  f"SoC: {data['percentage']*100:.1f}% | State: 0x{data['work_state']:02X}", end="", flush=True)
        else:
            print("\r[WARN] Waiting for Daemon data...", end="", flush=True)
        time.sleep(1.0)
