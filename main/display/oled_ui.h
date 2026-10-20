#pragma once
#include "easycon.h"
#include "hid_device.h"

// Pure rendering: page-major SH1106 framebuffer, no I/O or shared-state access.
void oled_ui_render(uint8_t frame[8][128], const easycon_script_status_t *status,
                    const hid_display_snapshot_t *input, bool usb_connected);
