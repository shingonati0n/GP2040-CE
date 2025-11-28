// gp_link.h
#pragma once

#include <stdint.h>

class Gamepad;

void GpLink_Init(void);

// Paquete A5: estado (inputMode + socdMode + auth)
void GpLink_UpdateStatus(uint8_t inputMode,
                         uint8_t socdMode,
                         uint8_t authMode);

// Paquete A6: history (input history como string)
void GpLink_SendHistory(const char* buf,
                        uint8_t len);

// Paquete A7: header (barra de estado / texto corto)
void GpLink_SendHeader(const char* buf,
                       uint8_t len);

// Tick ultra-liviano a llamar desde el loop principal
void GpLink_Tick(Gamepad* gamepad);
