#include "oled_ui.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Original 5x7 column glyphs for the status UI (LSB at top).
static const char glyph_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-/.+";
static const uint8_t glyphs[][5] = {
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3e,0x49,0x49,0x49,0x32}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x26,0x49,0x49,0x49,0x3e},
    {0x7e,0x09,0x09,0x09,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x00,0x41,0x7f,0x41,0x00}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x26,0x49,0x49,0x49,0x32}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x3f,0x40,0x30,0x40,0x3f}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43},
    {0x00,0x36,0x36,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x20,0x10,0x08,0x04,0x02},
    {0x00,0x60,0x60,0x00,0x00}, {0x08,0x08,0x3e,0x08,0x08},
};
_Static_assert(sizeof(glyphs) / sizeof(glyphs[0]) == sizeof(glyph_chars) - 1,
               "Font mapping mismatch");


typedef struct { uint8_t (*pixels)[128]; } canvas_t;

static void pixel(canvas_t *c, int x, int y, bool on) {
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    uint8_t mask = 1u << (y & 7);
    if (on) c->pixels[y / 8][x] |= mask;
    else c->pixels[y / 8][x] &= (uint8_t)~mask;
}
static void rect(canvas_t *c, int x, int y, int w, int h, bool on) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pixel(c, xx, yy, on);
}
static void text(canvas_t *c, int x, int y, const char *str, int scale, bool on) {
    while (*str && x + 5 * scale <= 128 && y + 7 * scale <= 64) {
        const char *g = strchr(glyph_chars, *str++);
        if (g) {
            const uint8_t *glyph = glyphs[g - glyph_chars];
            for (int col = 0; col < 5; ++col)
                for (int row = 0; row < 7; ++row)
                    if (glyph[col] & (1u << row))
                        rect(c, x + col * scale, y + row * scale, scale, scale, on);
        }
        x += 6 * scale;
    }
}
static void button(canvas_t *c, int x, int y, const char *name, bool down) {
    if (down) rect(c, x - 1, y, (int)strlen(name) * 6 + 1, 7, true);
    text(c, x, y, name, 1, !down);
}
static void circle(canvas_t *c, int cx, int cy, int radius) {
    int x = radius, y = 0, error = 1 - radius;
    while (x >= y) {
        pixel(c, cx+x, cy+y, true); pixel(c, cx+y, cy+x, true);
        pixel(c, cx-y, cy+x, true); pixel(c, cx-x, cy+y, true);
        pixel(c, cx-x, cy-y, true); pixel(c, cx-y, cy-x, true);
        pixel(c, cx+y, cy-x, true); pixel(c, cx+x, cy-y, true);
        ++y;
        if (error < 0) error += 2*y+1;
        else { --x; error += 2*(y-x)+1; }
    }
}
static int axis(uint8_t value) {
    int offset = (int)value - 128;
    return offset * 5 / (offset < 0 ? 128 : 127);
}
static void stick(canvas_t *c, int x, const char *name, uint8_t sx, uint8_t sy, bool down) {
    text(c, x == 45 ? 29 : 87, 51, name, 1, true);
    circle(c, x, 54, 8);
    if (down) circle(c, x, 54, 9);
    rect(c, x + axis(sx) - 1, 54 + axis(sy) - 1, 3, 3, true);
}
static void arrow(canvas_t *c, int cx, int cy, unsigned direction, bool down) {
    static const uint8_t up[] = {4, 14, 31, 4, 4};
    if (down) rect(c, cx-3, cy-3, 7, 7, true);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            if (!(up[y] & (1u << (4-x)))) continue;
            int dx = x-2, dy = y-2;
            for (unsigned i = 0; i < direction; ++i) {
                int old_x = dx; dx = -dy; dy = old_x;
            }
            pixel(c, cx+dx, cy+dy, !down);
        }
}
static void controller(canvas_t *c, const hid_display_snapshot_t *input) {
    uint16_t b = input->buttons;
    button(c, 1, 36, "ZL", b & SWITCH_BUTTON_ZL);
    button(c, 20, 36, "L", b & SWITCH_BUTTON_L);
    button(c, 40, 36, "-", b & SWITCH_BUTTON_MINUS);
    button(c, 51, 36, "+", b & SWITCH_BUTTON_PLUS);
    button(c, 64, 36, "H", b & SWITCH_BUTTON_HOME);
    button(c, 77, 36, "C", b & SWITCH_BUTTON_CAPTURE);
    button(c, 99, 36, "R", b & SWITCH_BUTTON_R);
    button(c, 111, 36, "ZR", b & SWITCH_BUTTON_ZR);
    arrow(c, 13, 47, 0, input->directions & 1);
    arrow(c, 21, 55, 1, input->directions & 2);
    arrow(c, 13, 60, 2, input->directions & 4);
    arrow(c, 5, 55, 3, input->directions & 8);
    button(c, 114, 43, "X", b & SWITCH_BUTTON_X);
    button(c, 106, 50, "Y", b & SWITCH_BUTTON_Y);
    button(c, 122, 50, "A", b & SWITCH_BUTTON_A);
    button(c, 114, 57, "B", b & SWITCH_BUTTON_B);
    stick(c, 45, "L", input->report.LX, input->report.LY, b & SWITCH_BUTTON_LCLICK);
    stick(c, 75, "R", input->report.RX, input->report.RY, b & SWITCH_BUTTON_RCLICK);
}
static const char *error_name(int code) {
    switch (code) {
    case 3: return "BAD IMAGE";
    case 4: return "BAD OPCODE";
    case 5: return "BAD SLOT";
    case 6: return "BAD TYPE";
    case 7: return "BAD INDEX";
    case 8: return "DIV BY ZERO";
    case 9: return "CALL DEPTH";
    case 10: return "NATIVE";
    case 11: return "HOST ERROR";
    case 12: return "NO MEMORY";
    case 13: case 14: return "CAPABILITY";
    default: return "ERR";
    }
}
void oled_ui_render(uint8_t frame[8][128], const easycon_script_status_t *status,
                    const hid_display_snapshot_t *input, bool usb_connected) {
    static const char *const states[] = {"READY", "RUNNING", "WAITING", "STOPPED", "FINISHED", "ERROR"};
    memset(frame, 0, 8 * 128);
    canvas_t canvas = {.pixels = frame};
    canvas_t *c = &canvas;
    bool running = status->state == EASYCON_SCRIPT_RUNNING || status->state == EASYCON_SCRIPT_WAITING;
    bool pc = status->pc_seen && !running;
    const char *state = pc ? (status->pc_age_ms < 2000 ? "ACTIVE" : "IDLE") :
        (unsigned)status->state < sizeof(states)/sizeof(states[0]) ? states[status->state] : "ERROR";
    if (!pc && !status->ecx && status->state == EASYCON_SCRIPT_FINISHED && status->end_address <= 2)
        state = "NO SCRIPT";
    text(c, 0, 0, pc ? "PC" : status->ecx ? "ECX" : "MCU", 1, true);
    text(c, 99, 0, "USB", 1, true);
    rect(c, 121, 0, 7, 7, true);
    if (!usb_connected) rect(c, 122, 1, 5, 5, false);
    text(c, 0, 10, state, 2, true);
    char line[32];
    if (pc) {
        if (status->pc_age_ms < 100000)
            snprintf(line, sizeof(line), "LAST %" PRIu32 ".%" PRIu32 "S", status->pc_age_ms/1000, (status->pc_age_ms/100)%10);
        else snprintf(line, sizeof(line), "LAST %" PRIu32 "S", status->pc_age_ms/1000);
        text(c, 0, 26, line, 1, true);
    } else if (status->state == EASYCON_SCRIPT_ERROR) {
        snprintf(line, sizeof(line), "%s %d", error_name(status->error_code), status->error_code);
        text(c, 0, 26, line, 1, true);
    } else {
        uint32_t seconds = status->elapsed_ms/1000;
        snprintf(line, sizeof(line), "T %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
                 seconds/3600, (seconds/60)%60, seconds%60);
        text(c, 0, 26, line, 1, true);
        if (status->state == EASYCON_SCRIPT_WAITING) {
            if (status->wait_ms < 100000)
                snprintf(line, sizeof(line), "W %" PRIu32 ".%" PRIu32 "S", status->wait_ms/1000, (status->wait_ms/100)%10);
            else if (status->wait_ms < 3600000)
                snprintf(line, sizeof(line), "W %" PRIu32 "S", status->wait_ms/1000);
            else snprintf(line, sizeof(line), "W %" PRIu32 "H", status->wait_ms/3600000);
            text(c, 80, 26, line, 1, true);
        }
    }
    rect(c, 0, 34, 128, 1, true);
    controller(c, input);
}
