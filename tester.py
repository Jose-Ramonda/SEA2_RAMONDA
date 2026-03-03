import threading
import serial
import struct
import time
import queue

# Variable de control global
ejecutando = True

#Macros
START_BYTE = 0xAA
NODO_ID = 0x0A
PORT = '/dev/ttyUSB1'
BAUD = 9600
POLLING_TIME = 1    #Polling de un segundo

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

def enviar_comando(comando_id, mensaje,ser,flag):
    try:
        payload = mensaje.encode('ascii')

        # Header: ID, CMD, SIZE
        header = struct.pack('BBB', NODO_ID, comando_id, len(payload))

        # CRC sobre HEADER + PAYLOAD (NO incluir START_BYTE)
        crc_val = crc16_esp(header + payload)
        if(flag == 0):
            crc_val = 0xFFFF

        # Trama final:
        # START + HEADER + PAYLOAD + CRC (little endian)
        trama = (
            struct.pack('B', START_BYTE)
            + header
            + payload
            + struct.pack('<H', crc_val)
        )

        
        ser.write(trama)
        
        #print(f"Enviando: {mensaje}")
        #print(f"CRC calculado: 0x{crc_val:04X}")
        #print(f"Trama completa (HEX): {trama.hex(' ')}")
        

    except Exception as e:
        print(f"Error: {e}")


def listener_serial(ser):
    global ejecutando

    while ejecutando:

        # Byte de start 0xAA
        byte = ser.read(1)

        if not byte:
            continue  # timeout, seguir esperando

        if byte[0] != START_BYTE:
            continue  # basura, ignorar

        # Leer encabezado
        header = ser.read(3)
        if len(header) != 3:
            continue  # trama incompleta

        payload_len = header[2]

        # 3 Leer payload
        payload = ser.read(payload_len)
        if len(payload) != payload_len:
            continue  # incompleto

        # 4 Leer CRC
        crc = ser.read(2)
        if len(crc) != 2:
            continue

        # Armar trama
        trama = byte + header + payload + crc

        # 6Imprimir 
        print("Trama :")
        print(" ".join(f"{b:02X}" for b in trama))
        print("Payload:")
        print(payload.decode(errors="replace"))
        print("-" * 40)

def sender(pol_time,ser,cola):
    while ejecutando:
        if cola.empty():
            enviar_comando(0,"",ser,1)    #Envio comando polling cmd0 payload vacio
        else:
            cmd = cola.get()
            match cmd:
                case "1":
                    enviar_comando(2,"",ser,1)    #Toglear led
                case "2":
                    enviar_comando(3,"",ser,1)    #Tarea demorona
                case "3":
                    enviar_comando(100,"YVAN EHT NIOJ",ser,1)    #Devuelve invertido
                case "4":
                    enviar_comando(100,"YVAN EHT NIOJ",ser,0) 
                case _: #default
                    enviar_comando(0,"",ser,1)    #Envio comando polling cmd0 payload vacio
        time.sleep(pol_time)                
    
def cmd_input(cola):
    while ejecutando:
        cmd = input()
        cola.put(cmd)


def main():
    global ejecutando

    # Crear objeto Serial (esto es un objeto de la clase Serial)
    ser = serial.Serial(
        port="/dev/ttyUSB1",  # cambiar si hace falta
        baudrate=9600,
        timeout=1
    )

    #Creo cola
    cola = queue.Queue()

    # Crear objeto Thread
    escucha = threading.Thread(target=listener_serial, args=(ser,))
    escucha.start()
    polleador = threading.Thread(target=sender, args=(POLLING_TIME,ser,cola,))
    polleador.start()
    comandador = threading.Thread(target=cmd_input, args=(cola,), daemon=True)
    #comandador = threading.Thread(target=cmd_input, args=(cola,))
    comandador.start()

    print("Leyendo puerto serie. Ctrl+C para salir.")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Cerrando...")
        ejecutando = False
        escucha.join()
        polleador.join()
        comandador.join()
        ser.close()
        print("Programa terminado")

if __name__ == "__main__":
    main()