# Power on: releases the CH340 USB relay. The relay sits in the C2's 5 V
# line wired COM->NC, so a released relay powers the board (as tools/power.py
# on does).
import serial
port = '/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0'
with serial.Serial(port, 9600) as s:
    s.write(bytes([0xA0, 0x01, 0x00, 0xA1]))  # relay released
