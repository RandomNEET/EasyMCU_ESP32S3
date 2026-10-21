#pragma once
#include "easycon.h"
#include "hid_device.h"

typedef enum {
  OLED_FACE_NEUTRAL,
  OLED_FACE_SMILE,
  OLED_FACE_CRY,
} oled_face_expression_t;

// Update only the centered 17x7 face region after drawing the dashboard.
void oled_render_face(uint8_t framebuffer[8][128], oled_face_expression_t expression,
                      bool eyes_closed);

// Pure rendering: page-major SH1106 framebuffer, no I/O or shared-state access.
void oled_render_dashboard(uint8_t framebuffer[8][128],
                           const easycon_script_status_t *script_status,
                           const hid_display_snapshot_t *controller_input, bool usb_connected);
