#pragma once
#include "easycon.h"
#include "hid_device.h"

// Pure rendering: page-major SH1106 framebuffer, no I/O or shared-state access.
void oled_render_dashboard(uint8_t framebuffer[8][128],
                           const easycon_script_status_t *script_status,
                           const hid_display_snapshot_t *controller_input, bool usb_connected);
