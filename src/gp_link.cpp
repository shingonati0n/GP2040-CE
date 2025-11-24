// gp_link.cpp

#include "gp_link.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdint.h>

#define GP_LINK_UART        uart0
#define GP_LINK_UART_TX_PIN 0
#define GP_LINK_BAUD        115200

// ---------------------------------------------------------------------------
// Inicialización del UART
// ---------------------------------------------------------------------------

void GpLink_Init(void) {
    uart_init(GP_LINK_UART, GP_LINK_BAUD);
    gpio_set_function(GP_LINK_UART_TX_PIN, GPIO_FUNC_UART);
}

// ---------------------------------------------------------------------------
// Paquete A5: STATUS (inputMode + SOCD + auth)
// ---------------------------------------------------------------------------
//
// Formato:
//   header   = 0xA5
//   status0  = [ bits 0–2: inputMode (0–7) ]
//              [ bits 3–4: socdMode (0–3)  ]
//   authMode = 0/1 (sin auth / con auth)
//   checksum = status0 ^ authMode
//
// Total: 4 bytes: [A5][status0][authMode][checksum]
// ---------------------------------------------------------------------------

void GpLink_UpdateStatus(uint8_t inputMode, uint8_t socdMode, uint8_t authMode) {
    // Compactar inputMode (3 bits) + socdMode (2 bits) en un solo byte
    uint8_t status0 = (inputMode & 0x07) | ((socdMode & 0x03) << 3);

    uint8_t chksum = status0 ^ authMode;

    uart_putc_raw(GP_LINK_UART, 0xA5);
    uart_putc_raw(GP_LINK_UART, status0);
    uart_putc_raw(GP_LINK_UART, authMode);
    uart_putc_raw(GP_LINK_UART, chksum);
}

// ---------------------------------------------------------------------------
// Helper para enviar frames de texto (A6 / A7)
// ---------------------------------------------------------------------------
//
// Formato común:
//   header = 0xA6 o 0xA7
//   len    = número de bytes de payload (<= 32)
//   data   = payload ASCII
//   chksum = len XOR data[0] XOR data[1] ... XOR data[len-1]
//
// Total: 2 + len + 1 bytes: [HDR][len][data...][chksum]
// ---------------------------------------------------------------------------

static void gp_link_send_frame(uint8_t header, const char* buf, uint8_t len) {
    if (!buf || len == 0) {
        return;
    }

    if (len > 32) {
        len = 32; // límite de seguridad
    }

    uint8_t chksum = len;
    for (uint8_t i = 0; i < len; i++) {
        chksum ^= (uint8_t)buf[i];
    }

    uart_putc_raw(GP_LINK_UART, header);
    uart_putc_raw(GP_LINK_UART, len);

    for (uint8_t i = 0; i < len; i++) {
        uart_putc_raw(GP_LINK_UART, (uint8_t)buf[i]);
    }

    uart_putc_raw(GP_LINK_UART, chksum);
}

// ---------------------------------------------------------------------------
// Paquete A6: HISTORY (input history como string)
// ---------------------------------------------------------------------------

void GpLink_SendHistory(const char* buf, uint8_t len) {
    gp_link_send_frame(0xA6, buf, len);
}

// ---------------------------------------------------------------------------
// Paquete A7: HEADER (statusBar de la pantalla)
// ---------------------------------------------------------------------------

void GpLink_SendHeader(const char* buf, uint8_t len) {
    gp_link_send_frame(0xA7, buf, len);
}
