from machine import UART, Pin
import time

LOG_TIME_MS = 15000  # 15 segundos de captura
UART_RX_PIN = 13     # tu pin ya usado por fight-lights
BAUD = 115200

uart = UART(0, baudrate=BAUD, tx=None, rx=Pin(UART_RX_PIN))

# Abre el archivo de log
log = open("uart_log.txt", "w")

log.write("=== GP2040 → Fight-Lights UART Logger ===\n")
log.write("Inicio de captura...\n\n")
log.flush()

start = time.ticks_ms()


def read_exact(n):
    """Lee n bytes exactos o retorna None."""
    data = uart.read(n)
    if not data or len(data) < n:
        return None
    return data


def decode_packet():
    """Intenta leer un paquete GPLink y logearlo."""
    if not uart.any():
        return

    header = uart.read(1)
    if not header:
        return

    h = header[0]

    # ---- STATUS (0xA5) ----
    if h == 0xA5:
        data = read_exact(3)
        if not data:
            log.write("STATUS INCOMPLETO\n")
            log.flush()
            return

        status0 = data[0]
        auth = data[1]
        chksum = data[2]

        if (status0 ^ auth) != chksum:
            log.write("STATUS checksum incorrecto\n")
            return

        input_mode = status0 & 0x07
        socd_mode  = (status0 >> 3) & 0x03

        log.write(f"[STATUS] input={input_mode} socd={socd_mode} auth={auth}\n")
        log.flush()
        return

    # ---- HISTORY (0xA6) ----
    elif h == 0xA6:
        ln_b = read_exact(1)
        if not ln_b:
            log.write("HISTORY length faltante\n")
            return

        length = ln_b[0]
        payload = read_exact(length + 1)
        if not payload:
            log.write("HISTORY incompleto\n")
            return

        chars = payload[:-1]
        chksum = payload[-1]

        calc = length
        for c in chars:
            calc ^= c

        if calc != chksum:
            log.write("HISTORY checksum incorrecto\n")
            return

        try:
            text = chars.decode("ascii", "ignore")
        except:
            text = "".join(chr(c) for c in chars)

        log.write(f"[HISTORY] \"{text}\"\n")
        log.flush()
        return

    # ---- HEADER (0xA7) ----
    elif h == 0xA7:
        ln_b = read_exact(1)
        if not ln_b:
            log.write("HEADER length faltante\n")
            return

        length = ln_b[0]
        payload = read_exact(length + 1)
        if not payload:
            log.write("HEADER incompleto\n")
            return

        chars = payload[:-1]
        chksum = payload[-1]

        calc = length
        for c in chars:
            calc ^= c

        if calc != chksum:
            log.write("HEADER checksum incorrecto\n")
            return

        try:
            text = chars.decode("ascii", "ignore")
        except:
            text = "".join(chr(c) for c in chars)

        log.write(f"[HEADER] \"{text}\"\n")
        log.flush()
        return

    # ---- Header desconocido ----
    else:
        log.write(f"[RAW] Encabezado desconocido: {hex(h)}\n")
        log.flush()
        return


# -------------------
# Loop principal
# -------------------
while time.ticks_diff(time.ticks_ms(), start) < LOG_TIME_MS:
    decode_packet()
    time.sleep_ms(2)

log.write("\nFin de captura.\n")
log.close()
