import serial
import struct
import sys

# --- CONFIGURACIÓN ---
PORT = '/dev/ttyUSB1'
BAUD = 9600
START_BYTE = 0xAA
NODO_ID = 0x0A


# ============================================================
# CRC16 equivalente a esp_rom_crc16_le()
# ============================================================

def crc16_esp(data: bytes, seed=0xFFFF):
    crc = (~seed) & 0xFFFF   # igual que en ROM

    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x8408
            else:
                crc >>= 1
            crc &= 0xFFFF

    return (~crc) & 0xFFFF


# ============================================================
# ENVÍO DE COMANDO
# ============================================================

def enviar_comando(comando_id, mensaje):
    try:
        payload = mensaje.encode('ascii')

        # Header: ID, CMD, SIZE
        header = struct.pack('BBB', NODO_ID, comando_id, len(payload))

        # CRC sobre HEADER + PAYLOAD (NO incluir START_BYTE)
        crc_val = crc16_esp(header + payload)

        # Trama final:
        # START + HEADER + PAYLOAD + CRC (little endian)
        trama = (
            struct.pack('B', START_BYTE)
            + header
            + payload
            + struct.pack('<H', crc_val)
        )

        with serial.Serial(PORT, BAUD, timeout=1) as ser:
            ser.write(trama)

        print(f"Enviando: {mensaje}")
        print(f"CRC calculado: 0x{crc_val:04X}")
        print(f"Trama completa (HEX): {trama.hex(' ')}")
        

    except Exception as e:
        print(f"Error: {e}")


# ============================================================
# MAIN
# ============================================================

if len(sys.argv) > 2:
    id_cmd = int(sys.argv[1])
    texto = sys.argv[2]
    enviar_comando(id_cmd, texto)
    data = bytes.fromhex("0a01044a414a41")
    print(hex(crc16_esp(data)))