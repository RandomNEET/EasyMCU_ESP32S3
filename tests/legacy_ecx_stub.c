// Legacy interpreter tests explicitly run with no ECX program installed.
#include "ecx_runtime.h"
bool ecx_present(void) { return false; }
bool ecx_auto_run(void) { return false; }
bool ecx_busy(void) { return false; }
bool ecx_start(void) { return false; }
void ecx_stop(void) {}
void ecx_poll(void) {}
void ecx_status(easycon_script_status_t *status) { (void)status; }
void ecx_rx_begin(void) {}
bool ecx_rx_byte(uint8_t byte) { (void)byte; return false; }
