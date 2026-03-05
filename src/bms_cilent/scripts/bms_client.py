#!/usr/bin/env python3
import socket
import struct
import os
import time

class BmsClient:
    """
    BMS Daemon Python 客户端接口
    用于通过 Unix Domain Socket 读取电池数据
    """
    # 结构体格式定义 (对应 C++ 中的 #pragma pack(push, 1))
    # d: double (8 bytes), I: uint32 (4 bytes), H: uint16 (2 bytes), 33s: char[33]
    # 总长度: 121 字节
    STRUCT_FORMAT = "<dddddddIHdd33sHHHI"
    STRUCT_SIZE = struct.calcsize(STRUCT_FORMAT)

    def __init__(self, socket_path="/tmp/bms.sock"):
        self.socket_path = socket_path
        self.sock = None

    def connect(self):
        """尝试连接到 Daemon"""
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
        """关闭连接"""
        if self.sock:
            self.sock.close()
            self.sock = None

    def read_data(self):
        """
        读取并解析一次数据
        返回 dict 格式的结果，如果失败则返回 None
        """
        if not self.sock:
            if not self.connect():
                return None

        try:
            data = self.sock.recv(self.STRUCT_SIZE)
            if len(data) < self.STRUCT_SIZE:
                self.close()
                return None

            # 解析二进制数据
            unpacked = struct.unpack(self.STRUCT_FORMAT, data)
            
            # 映射到字典
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
    # --- 简单的测试运行逻辑 ---
    print("=== BMS Python Client Test ===")
    client = BmsClient()
    
    while True:
        data = client.read_data()
        if data:
            print(f"\r[数据] 电压: {data['voltage']:.2f}V | 电流: {data['current']:.2f}A | "
                  f"电量: {data['percentage']*100:.1f}% | 状态: 0x{data['work_state']:02X}", end="", flush=True)
        else:
            print("\r[警告] 正在等待 Daemon 数据...", end="", flush=True)
        time.sleep(1.0)
