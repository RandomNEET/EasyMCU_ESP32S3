#pragma once
#include "easycon.h"
#include "esp_err.h"

#define CMD_ECX 0x90
#define ECX_MAX_IMAGE 65536
#define ECX_MAX_PAYLOAD 260

esp_err_t ecx_runtime_init(void);
bool ecx_present(void);
bool ecx_auto_run(void);
bool ecx_busy(void);
bool ecx_start(void);
void ecx_stop(void);
void ecx_poll(void);
void ecx_status(easycon_script_status_t *status);
void ecx_rx_begin(void);
bool ecx_rx_byte(uint8_t byte);
