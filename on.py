import serial, glob
port = '/dev/ttyUSB0'
with serial.Serial(port, 9600) as s:
    #s.write(bytes([0xA0, 0x01, 0x01, 0xA2]))   # ON
    s.write(bytes([0xA0, 0x01, 0x00, 0xA1])) # OFF
