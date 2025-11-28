// gp_link.cpp

#include "gp_link.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include "drivermanager.h"
#include "gamepad.h"
#include "storagemanager.h"
#include "drivers/ps4/PS4Driver.h"
#include "drivers/xbone/XBOneDriver.h"
#include "drivers/xinput/XInputDriver.h"

#include <string>
#include <string.h>
#include <stdint.h>

#define GP_LINK_UART        uart0
#define GP_LINK_UART_TX_PIN 0
#define GP_LINK_BAUD        115200

// ---------------------------------------------------------------------------
// Inicialización del UART (TX = GPIO0, 115200 baud)
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
//   authMode = 0/1 (sin auth / con auth enviado)
//   checksum = status0 ^ authMode
//
// Total: 4 bytes: [A5][status0][authMode][checksum]
// ---------------------------------------------------------------------------

void GpLink_UpdateStatus(uint8_t inputMode, uint8_t socdMode, uint8_t authMode) {
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
// Formato:
//   header = 0xA6 (history) o 0xA7 (header)
//   len    = número de bytes de payload (<= 32)
//   data   = payload ASCII
//   chksum = len XOR data[0] XOR ... XOR data[len-1]
//
// Total: 2 + len + 1 bytes = [HDR][len][data...][chksum]
// ---------------------------------------------------------------------------

static void gp_link_send_frame(uint8_t header, const char* buf, uint8_t len) {
    if (!buf || len == 0) {
        return;
    }

    if (len > 32) {
        len = 32;
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

void GpLink_SendHistory(const char* buf, uint8_t len) {
    gp_link_send_frame(0xA6, buf, len);
}

void GpLink_SendHeader(const char* buf, uint8_t len) {
    gp_link_send_frame(0xA7, buf, len);
}

// ---------------------------------------------------------------------------
// Tick principal: llamado desde el loop general
//
// - Solo envía STATUS si:
//     * cambió inputMode / SOCD / auth, o
//     * han pasado >= 100 ms desde el último envío
//
// - Solo envía HEADER textual si:
//     * cambió el string, y
//     * han pasado >= 100 ms desde el último envío de header
//
// Esto limita muchísimo la carga y evita cualquier impacto en el timing.
// ---------------------------------------------------------------------------

extern uint32_t getMillis(); // ya existe en el proyecto

void GpLink_Tick(Gamepad* gamepad) {
    if (!gamepad) return;

    static uint8_t  lastInputMode = 0xFF;
    static uint8_t  lastSocdMode  = 0xFF;
    static uint8_t  lastAuthMode  = 0xFF;
    static uint32_t lastStatusMs  = 0;
    static uint32_t lastHeaderMs  = 0;
    static std::string lastHeader;

    uint32_t now = getMillis();

    // --- 1. Calcular estado actual ---

    uint8_t inputMode = static_cast<uint8_t>(DriverManager::getInstance().getInputMode());
    const GamepadOptions& options = gamepad->getOptions();
    uint8_t socd = static_cast<uint8_t>(Gamepad::resolveSOCDMode(options));

    uint8_t authMode = 0;
    auto* driver = DriverManager::getInstance().getDriver();

    switch (inputMode) {
        case INPUT_MODE_PS4:
        case INPUT_MODE_PS5:
            if (driver && ((PS4Driver*)driver)->getAuthSent())
                authMode = 1;
            break;

        case INPUT_MODE_XBONE:
            if (driver && ((XBOneDriver*)driver)->getAuthSent())
                authMode = 1;
            break;

        case INPUT_MODE_XINPUT:
            if (driver && ((XInputDriver*)driver)->getAuthSent())
                authMode = 1;
            break;

        default:
            authMode = 0;
            break;
    }

    bool statusChanged =
        (inputMode != lastInputMode) ||
        (socd      != lastSocdMode)  ||
        (authMode  != lastAuthMode);

    // --- 2. Enviar STATUS (A5) con rate limit de 100 ms ---

    if (statusChanged || (now - lastStatusMs) >= 100) {
        GpLink_UpdateStatus(inputMode, socd, authMode);

        lastInputMode = inputMode;
        lastSocdMode  = socd;
        lastAuthMode  = authMode;
        lastStatusMs  = now;
    }

    // --- 3. Construir header textual (muy corto) ---

    std::string header;

    // Input mode en texto corto
    switch (inputMode) {
        case INPUT_MODE_PS3:          header += "PS3";    break;
        case INPUT_MODE_GENERIC:      header += "USBHID"; break;
        case INPUT_MODE_SWITCH:       header += "SWITCH"; break;
        case INPUT_MODE_MDMINI:       header += "GEN/MD"; break;
        case INPUT_MODE_NEOGEO:       header += "NGMINI"; break;
        case INPUT_MODE_PCEMINI:      header += "PCE/TG"; break;
        case INPUT_MODE_EGRET:        header += "EGRET";  break;
        case INPUT_MODE_ASTRO:        header += "ASTRO";  break;
        case INPUT_MODE_PSCLASSIC:    header += "PSC";    break;
        case INPUT_MODE_XBOXORIGINAL: header += "OGXBOX"; break;
        case INPUT_MODE_SWITCH_PRO:   header += "SWPRO";  break;
        case INPUT_MODE_PS4:          header += "PS4";    break;
        case INPUT_MODE_PS5:          header += "PS5";    break;
        case INPUT_MODE_XBONE:        header += "XBONE";  break;
        case INPUT_MODE_XINPUT:       header += "XINPUT"; break;
        case INPUT_MODE_KEYBOARD:     header += "HID-KB"; break;
        case INPUT_MODE_CONFIG:       header += "CONFIG"; break;
        default:                      header += "UNK";    break;
    }

    // SOCD breve
    switch (Gamepad::resolveSOCDMode(options)) {
        case SOCD_MODE_NEUTRAL:               header += " N"; break;
        case SOCD_MODE_UP_PRIORITY:           header += " U"; break;
        case SOCD_MODE_SECOND_INPUT_PRIORITY: header += " L"; break;
        case SOCD_MODE_FIRST_INPUT_PRIORITY:  header += " F"; break;
        case SOCD_MODE_BYPASS:                header += " X"; break;
    }

    // Sufijo de auth simple
    if (authMode) {
        header += " A";
    }

    // Limitar longitud
    if (header.size() > 20) {
        header.resize(20);
    }

    bool headerChanged = (header != lastHeader);
    
    // --- 4. Enviar HEADER (A7) si cambió O cada 100 ms ---
    
    if (!header.empty() && (headerChanged || (now - lastHeaderMs) >= 100)) {
        GpLink_SendHeader(
            header.c_str(),
            static_cast<uint8_t>(header.size())
        );
        lastHeader   = header;
        lastHeaderMs = now;
    }
}
