// gp_link.h
#pragma once

#include <stdint.h>

void GpLink_Init(void);

void GpLink_UpdateStatus(uint8_t inputMode,
                         uint8_t socdMode,
                         uint8_t authMode);

void GpLink_SendHistory(const char* buf,
                        uint8_t len);

void GpLink_SendHeader(const char* buf,
                       uint8_t len);
