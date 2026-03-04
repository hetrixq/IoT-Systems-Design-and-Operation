#!/usr/bin/env python3
"""
Simple Modbus RTU master for Wokwi (via RFC2217 -> UART0).
Usage:    python3 modbus_master.py
Requires: pip install pyserial
"""

import struct
import sys
import time

try:
    import serial
except ImportError:
    print("Install pyserial first:  pip3 install pyserial")
    sys.exit(1)


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def build_frame(payload: bytes) -> bytes:
    c = crc16(payload)
    return payload + bytes([c & 0xFF, (c >> 8) & 0xFF])


def parse_response(data: bytes) -> str:
    if len(data) < 4:
        return f"  Response too short: {data.hex(' ').upper()}"
    c_rx = data[-2] | (data[-1] << 8)
    c_ok = crc16(data[:-2])
    if c_rx != c_ok:
        return f"  CRC error! {data.hex(' ').upper()}"
    func = data[1]
    if func & 0x80:
        codes = {
            1: "illegal function",
            2: "illegal data address",
            3: "illegal data value",
            4: "device failure",
        }
        return f"  EXCEPTION {data[2]:02X}: {codes.get(data[2], '?')}"
    if func == 0x03:
        n = data[2]
        regs = [struct.unpack_from(">H", data, 3 + i * 2)[0] for i in range(n // 2)]
        return "  Registers: " + ", ".join(
            f"reg[{i}]={v} (0x{v:04X})" for i, v in enumerate(regs)
        )
    if func in (0x06, 0x10):
        addr = struct.unpack_from(">H", data, 2)[0]
        val = struct.unpack_from(">H", data, 4)[0]
        return f"  OK: wrote 0x{val:04X} to address 0x{addr:04X}"
    return f"  {data.hex(' ').upper()}"


MENU = """
=== Modbus RTU Master (Wokwi RFC2217) ===
  1  Write R (holding[0]) = max (8191)
  2  Write G (holding[1]) = max
  3  Write B (holding[2]) = max
  4  All channels = 50% (4095)
  5  All channels = 0 (off)
  6  Read 3 holding registers (FC03, addr=0)
  7  Read 8 registers from 0x0100
  8  Enter custom HEX frame
  q  Quit
"""

FRAMES = {
    "1": build_frame(bytes([0x01, 0x06, 0x00, 0x00, 0x1F, 0xFF])),
    "2": build_frame(bytes([0x01, 0x06, 0x00, 0x01, 0x1F, 0xFF])),
    "3": build_frame(bytes([0x01, 0x06, 0x00, 0x02, 0x1F, 0xFF])),
    "4": build_frame(
        bytes(
            [
                0x01,
                0x10,
                0x00,
                0x00,
                0x00,
                0x03,
                0x06,
                0x0F,
                0xFF,
                0x0F,
                0xFF,
                0x0F,
                0xFF,
            ]
        )
    ),
    "5": build_frame(
        bytes(
            [
                0x01,
                0x10,
                0x00,
                0x00,
                0x00,
                0x03,
                0x06,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
                0x00,
            ]
        )
    ),
    "6": build_frame(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x03])),
    "7": build_frame(bytes([0x01, 0x03, 0x01, 0x00, 0x00, 0x08])),
}

HOST = "localhost"
PORT = 4000


def main():
    print(f"Connecting to rfc2217://{HOST}:{PORT} ...")
    try:
        ser = serial.serial_for_url(
            f"rfc2217://{HOST}:{PORT}", baudrate=115200, timeout=1.0
        )
    except Exception as e:
        print(f"Connection error: {e}")
        print("Make sure the Wokwi simulation is running (Wokwi: Start Simulator)")
        sys.exit(1)

    print("Connected!\n")

    try:
        while True:
            print(MENU)
            choice = input(">>> ").strip().lower()

            if choice == "q":
                break

            frame = None
            if choice in FRAMES:
                frame = FRAMES[choice]
            elif choice == "8":
                raw = input("HEX (no CRC, e.g. 01 06 00 00 1F FF): ").strip()
                try:
                    payload = bytes.fromhex(raw.replace(" ", ""))
                    frame = build_frame(payload)
                except ValueError:
                    print("  HEX parse error")
                    continue
            else:
                print("  Invalid choice")
                continue

            print(f"  TX: {frame.hex(' ').upper()}")
            ser.reset_input_buffer()
            ser.write(frame)
            time.sleep(0.3)
            resp = ser.read(ser.in_waiting or 64)
            if resp:
                print(f"  RX: {resp.hex(' ').upper()}")
                print(parse_response(resp))
            else:
                print("  No response (timeout)")
    finally:
        ser.close()
        print("Disconnected.")


if __name__ == "__main__":
    main()
