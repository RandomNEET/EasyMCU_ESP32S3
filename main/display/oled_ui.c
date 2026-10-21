#include "oled_ui.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Original 5x7 column glyphs for the script_status UI (LSB at top).
static const char glyph_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-/.+";
static const uint8_t glyphs[][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00}, {0x62, 0x51, 0x49, 0x49, 0x46},
    {0x22, 0x41, 0x49, 0x49, 0x36}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3e, 0x49, 0x49, 0x49, 0x32}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x26, 0x49, 0x49, 0x49, 0x3e}, {0x7e, 0x09, 0x09, 0x09, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x26, 0x49, 0x49, 0x49, 0x32}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x30, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43},
    {0x00, 0x36, 0x36, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x00, 0x60, 0x60, 0x00, 0x00}, {0x08, 0x08, 0x3e, 0x08, 0x08},
};
_Static_assert(sizeof(glyphs) / sizeof(glyphs[0]) == sizeof(glyph_chars) - 1,
               "Font mapping mismatch");

typedef struct {
  uint8_t (*pixels)[128];
} oled_canvas_t;

enum { FACE_X = (128 - 17) / 2 };

static void draw_pixel(oled_canvas_t *canvas, int x, int y, bool on) {
  if (x < 0 || x >= 128 || y < 0 || y >= 64)
    return;
  uint8_t mask = 1u << (y & 7);
  if (on)
    canvas->pixels[y / 8][x] |= mask;
  else
    canvas->pixels[y / 8][x] &= (uint8_t)~mask;
}
static void fill_rect(oled_canvas_t *canvas, int x, int y, int w, int h, bool on) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx)
      draw_pixel(canvas, xx, yy, on);
}
static void draw_text(oled_canvas_t *canvas, int x, int y, const char *str, int scale, bool on) {
  while (*str && x + 5 * scale <= 128 && y + 7 * scale <= 64) {
    const char *g = strchr(glyph_chars, *str++);
    if (g) {
      const uint8_t *glyph = glyphs[g - glyph_chars];
      for (int col = 0; col < 5; ++col)
        for (int row = 0; row < 7; ++row)
          if (glyph[col] & (1u << row))
            fill_rect(canvas, x + col * scale, y + row * scale, scale, scale, on);
    }
    x += 6 * scale;
  }
}
// Fixed 5x7 draw_pixel approximation of U+15DC, with a low underscore mouth.
// The ASCII draw_text renderer does not decode UTF-8; keep this mark independent.
static void draw_face_mark(oled_canvas_t *canvas, oled_face_expression_t expression,
                           bool eyes_closed) {
  static const uint8_t eye[] = {0x1f, 0x25, 0x25, 0x25, 0x1f};
  for (int col = 0; col < 5; ++col)
    for (int row = 0; row < 7; ++row)
      if (eyes_closed ? row == 3 : (eye[col] & (1u << row)) != 0) {
        draw_pixel(canvas, FACE_X + col, row, true);
        draw_pixel(canvas, FACE_X + col + 12, row, true);
      }
  if (expression == OLED_FACE_NEUTRAL) {
    fill_rect(canvas, FACE_X + 6, 6, 5, 1, true);
  } else {
    for (int col = 0; col < 5; ++col) {
      int slope = col < 2 ? col : 4 - col;
      int y = expression == OLED_FACE_SMILE ? 4 + slope : 6 - slope;
      draw_pixel(canvas, FACE_X + 6 + col, y, true);
    }
  }
}

void oled_render_face(uint8_t framebuffer[8][128], oled_face_expression_t expression,
                      bool eyes_closed) {
  oled_canvas_t canvas = {.pixels = framebuffer};
  fill_rect(&canvas, FACE_X, 0, 17, 7, false);
  draw_face_mark(&canvas, expression, eyes_closed);
}

static void draw_button(oled_canvas_t *canvas, int x, int y, const char *name, bool down) {
  if (down)
    fill_rect(canvas, x - 1, y, (int)strlen(name) * 6 + 1, 7, true);
  draw_text(canvas, x, y, name, 1, !down);
}
static void draw_circle(oled_canvas_t *canvas, int cx, int cy, int radius) {
  int x = radius, y = 0, error = 1 - radius;
  while (x >= y) {
    draw_pixel(canvas, cx + x, cy + y, true);
    draw_pixel(canvas, cx + y, cy + x, true);
    draw_pixel(canvas, cx - y, cy + x, true);
    draw_pixel(canvas, cx - x, cy + y, true);
    draw_pixel(canvas, cx - x, cy - y, true);
    draw_pixel(canvas, cx - y, cy - x, true);
    draw_pixel(canvas, cx + y, cy - x, true);
    draw_pixel(canvas, cx + x, cy - y, true);
    ++y;
    if (error < 0)
      error += 2 * y + 1;
    else {
      --x;
      error += 2 * (y - x) + 1;
    }
  }
}
static int stick_axis_offset(uint8_t value) {
  int offset = (int)value - 128;
  return offset * 5 / (offset < 0 ? 128 : 127);
}
static void draw_stick(oled_canvas_t *canvas, int x, const char *name, uint8_t sx, uint8_t sy,
                       bool down) {
  draw_text(canvas, x == 45 ? 29 : 87, 51, name, 1, true);
  draw_circle(canvas, x, 54, 8);
  if (down)
    draw_circle(canvas, x, 54, 9);
  fill_rect(canvas, x + stick_axis_offset(sx) - 1, 54 + stick_axis_offset(sy) - 1, 3, 3, true);
}
static void draw_direction_arrow(oled_canvas_t *canvas, int cx, int cy, unsigned direction,
                                 bool down) {
  static const uint8_t up[] = {4, 14, 31, 4, 4};
  if (down)
    fill_rect(canvas, cx - 3, cy - 3, 7, 7, true);
  for (int y = 0; y < 5; ++y)
    for (int x = 0; x < 5; ++x) {
      if (!(up[y] & (1u << (4 - x))))
        continue;
      int dx = x - 2, dy = y - 2;
      for (unsigned i = 0; i < direction; ++i) {
        int old_x = dx;
        dx = -dy;
        dy = old_x;
      }
      draw_pixel(canvas, cx + dx, cy + dy, !down);
    }
}
static void draw_controller(oled_canvas_t *canvas, const hid_display_snapshot_t *controller_input) {
  uint16_t buttons = controller_input->buttons;
  draw_button(canvas, 1, 36, "ZL", buttons & SWITCH_BUTTON_ZL);
  draw_button(canvas, 20, 36, "L", buttons & SWITCH_BUTTON_L);
  draw_button(canvas, 40, 36, "-", buttons & SWITCH_BUTTON_MINUS);
  draw_button(canvas, 51, 36, "+", buttons & SWITCH_BUTTON_PLUS);
  draw_button(canvas, 64, 36, "H", buttons & SWITCH_BUTTON_HOME);
  draw_button(canvas, 77, 36, "C", buttons & SWITCH_BUTTON_CAPTURE);
  draw_button(canvas, 99, 36, "R", buttons & SWITCH_BUTTON_R);
  draw_button(canvas, 111, 36, "ZR", buttons & SWITCH_BUTTON_ZR);
  draw_direction_arrow(canvas, 13, 47, 0, controller_input->directions & 1);
  draw_direction_arrow(canvas, 21, 55, 1, controller_input->directions & 2);
  draw_direction_arrow(canvas, 13, 60, 2, controller_input->directions & 4);
  draw_direction_arrow(canvas, 5, 55, 3, controller_input->directions & 8);
  draw_button(canvas, 114, 43, "X", buttons & SWITCH_BUTTON_X);
  draw_button(canvas, 106, 50, "Y", buttons & SWITCH_BUTTON_Y);
  draw_button(canvas, 122, 50, "A", buttons & SWITCH_BUTTON_A);
  draw_button(canvas, 114, 57, "B", buttons & SWITCH_BUTTON_B);
  draw_stick(canvas, 45, "L", controller_input->report.LX, controller_input->report.LY,
             buttons & SWITCH_BUTTON_LCLICK);
  draw_stick(canvas, 75, "R", controller_input->report.RX, controller_input->report.RY,
             buttons & SWITCH_BUTTON_RCLICK);
}
static const char *script_error_label(int code) {
  switch (code) {
  case 3:
    return "BAD IMAGE";
  case 4:
    return "BAD OPCODE";
  case 5:
    return "BAD SLOT";
  case 6:
    return "BAD TYPE";
  case 7:
    return "BAD INDEX";
  case 8:
    return "DIV BY ZERO";
  case 9:
    return "CALL DEPTH";
  case 10:
    return "NATIVE";
  case 11:
    return "HOST ERROR";
  case 12:
    return "NO MEMORY";
  case 13:
  case 14:
    return "CAPABILITY";
  default:
    return "ERR";
  }
}
void oled_render_dashboard(uint8_t framebuffer[8][128],
                           const easycon_script_status_t *script_status,
                           const hid_display_snapshot_t *controller_input, bool usb_connected) {
  static const char *const state_labels[] = {"READY",   "RUNNING",  "WAITING",
                                             "STOPPED", "FINISHED", "ERROR"};
  memset(framebuffer, 0, 8 * 128);
  oled_canvas_t surface = {.pixels = framebuffer};
  oled_canvas_t *canvas = &surface;
  bool is_script_running = script_status->state == EASYCON_SCRIPT_RUNNING ||
                           script_status->state == EASYCON_SCRIPT_WAITING;
  bool is_online_control = script_status->pc_seen && !is_script_running;
  const char *state_label =
      is_online_control ? (script_status->pc_age_ms < 2000 ? "ACTIVE" : "IDLE")
      : (unsigned)script_status->state < sizeof(state_labels) / sizeof(state_labels[0])
          ? state_labels[script_status->state]
          : "ERROR";
  if (!is_online_control && !script_status->ecx &&
      script_status->state == EASYCON_SCRIPT_FINISHED && script_status->end_address <= 2)
    state_label = "NO SCRIPT";
  draw_face_mark(canvas, OLED_FACE_NEUTRAL, false);
  bool has_offline_session =
      is_script_running || script_status->ecx || script_status->end_address > 2;
  const char *mode_label = is_online_control     ? "ONLINE"
                           : has_offline_session ? "OFFLINE"
                                                 : "STANDBY";
  draw_text(canvas, 0, 0, mode_label, 1, true);
  draw_text(canvas, 108, 0, "NS", 1, true);
  fill_rect(canvas, 121, 0, 7, 7, true);
  if (!usb_connected)
    fill_rect(canvas, 122, 1, 5, 5, false);
  draw_text(canvas, 0, 10, state_label, 2, true);
  char detail_text[32];
  if (is_online_control) {
    if (script_status->pc_age_ms < 100000)
      snprintf(detail_text, sizeof(detail_text), "LAST %" PRIu32 ".%" PRIu32 "S",
               script_status->pc_age_ms / 1000, (script_status->pc_age_ms / 100) % 10);
    else
      snprintf(detail_text, sizeof(detail_text), "LAST %" PRIu32 "S",
               script_status->pc_age_ms / 1000);
    draw_text(canvas, 0, 26, detail_text, 1, true);
  } else if (script_status->state == EASYCON_SCRIPT_ERROR) {
    snprintf(detail_text, sizeof(detail_text), "%s %d",
             script_error_label(script_status->error_code), script_status->error_code);
    draw_text(canvas, 0, 26, detail_text, 1, true);
  } else {
    uint32_t seconds = script_status->elapsed_ms / 1000;
    snprintf(detail_text, sizeof(detail_text), "T %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
             seconds / 3600, (seconds / 60) % 60, seconds % 60);
    draw_text(canvas, 0, 26, detail_text, 1, true);
    if (script_status->state == EASYCON_SCRIPT_WAITING) {
      if (script_status->wait_ms < 100000)
        snprintf(detail_text, sizeof(detail_text), "W %" PRIu32 ".%" PRIu32 "S",
                 script_status->wait_ms / 1000, (script_status->wait_ms / 100) % 10);
      else if (script_status->wait_ms < 3600000)
        snprintf(detail_text, sizeof(detail_text), "W %" PRIu32 "S", script_status->wait_ms / 1000);
      else
        snprintf(detail_text, sizeof(detail_text), "W %" PRIu32 "H",
                 script_status->wait_ms / 3600000);
      draw_text(canvas, 80, 26, detail_text, 1, true);
    }
  }
  fill_rect(canvas, 0, 34, 128, 1, true);
  draw_controller(canvas, controller_input);
}
