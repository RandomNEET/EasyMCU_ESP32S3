#include "oled_ui.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t frame_t[8][128];
static struct {
  uint32_t before;
  frame_t frame;
  uint32_t after;
} guarded;
static easycon_script_status_t state;
static hid_display_snapshot_t input = {
    .report = {.HAT = 8, .LX = 128, .LY = 128, .RX = 128, .RY = 128}};
static void render(void) {
  guarded.before = 0x12345678;
  guarded.after = 0x98765432;
  oled_render_dashboard(guarded.frame, &state, &input, false);
  assert(guarded.before == 0x12345678 && guarded.after == 0x98765432);
}
static bool pixel(const frame_t f, int x, int y) { return (f[y / 8][x] >> (y % 8)) & 1; }
static void check_changes(const frame_t a, int x, int y, int w, int h) {
  unsigned changes = 0;
  for (int yy = 0; yy < 64; yy++)
    for (int xx = 0; xx < 128; xx++)
      if (pixel(a, xx, yy) != pixel(guarded.frame, xx, yy)) {
        assert(xx >= x && xx < x + w && yy >= y && yy < y + h);
        ++changes;
      }
  assert(changes > 0);
}
static void preview(const char *path) {
  FILE *file = fopen(path, "wb");
  assert(file);
  fprintf(file, "P5\n384 128\n255\n");
  frame_t frames[6];
  for (unsigned i = 0; i < 6; i++) {
    state = (easycon_script_status_t){
        .pc_hat = 8, .pc_lx = 128, .pc_ly = 128, .pc_rx = 128, .pc_ry = 128};
    input.buttons = 0;
    input.directions = 0;
    if (i == 1 || i == 2) {
      state.pc_seen = true;
      state.pc_age_ms = i == 1 ? 100 : 60000;
    }
    if (i >= 3) {
      state.ecx = true;
      state.elapsed_ms = 12500;
      state.state = EASYCON_SCRIPT_RUNNING;
    }
    if (i == 1) {
      input.buttons = SWITCH_BUTTON_A | SWITCH_BUTTON_ZL;
      input.directions = 3;
    }
    if (i == 4) {
      state.state = EASYCON_SCRIPT_WAITING;
      state.wait_ms = 1500;
    }
    if (i == 5) {
      state.state = EASYCON_SCRIPT_ERROR;
      state.error_code = 8;
    }
    oled_render_dashboard(frames[i], &state, &input, i != 0);
  }
  for (int y = 0; y < 128; y++)
    for (int x = 0; x < 384; x++)
      fputc(pixel(frames[(y / 64) * 3 + x / 128], x % 128, y % 64) ? 255 : 0, file);
  fclose(file);
}
int main(int argc, char **argv) {
  render();
  frame_t neutral;
  memcpy(neutral, guarded.frame, sizeof(neutral));
  // Standby hides timing until a control source is known.
  state.elapsed_ms = UINT32_MAX;
  render();
  assert(!memcmp(neutral, guarded.frame, sizeof(neutral)));
  state.ecx = true;
  state.elapsed_ms = 0;
  render();
  frame_t offline_ready;
  memcpy(offline_ready, guarded.frame, sizeof(offline_ready));
  state.elapsed_ms = UINT32_MAX;
  render();
  check_changes(offline_ready, 93, 19, 35, 7);
  state.ecx = false;
  state.elapsed_ms = 0;
  render();
  // Expressions and blinking must never alter status text or controller pixels.
  for (int expression = OLED_FACE_NEUTRAL; expression <= OLED_FACE_CRY; ++expression) {
    for (int closed = 0; closed <= 1; ++closed) {
      render();
      oled_render_face(guarded.frame, expression, closed);
      if (expression != OLED_FACE_NEUTRAL || closed)
        check_changes(neutral, 55, 0, 17, 7);
      if (closed) {
        assert(pixel(guarded.frame, 55, 3));
        assert(!pixel(guarded.frame, 55, 0));
      }
      if (expression == OLED_FACE_SMILE)
        assert(pixel(guarded.frame, 61, 4) && pixel(guarded.frame, 63, 6));
      if (expression == OLED_FACE_CRY)
        assert(pixel(guarded.frame, 61, 6) && pixel(guarded.frame, 63, 4));
    }
  }
  render();
  // Region checks protect all 14 mappings, without mirroring font bitmaps.
  const int regions[14][4] = {{104, 50, 7, 7}, {111, 57, 7, 7}, {118, 50, 7, 7},  {111, 43, 7, 7},
                              {19, 31, 7, 7},  {102, 31, 7, 7}, {0, 31, 13, 7},   {115, 31, 13, 7},
                              {41, 33, 7, 7},  {80, 33, 7, 7},  {36, 45, 19, 19}, {73, 45, 19, 19},
                              {67, 33, 7, 7},  {54, 33, 7, 7}};
  for (unsigned b = 0; b < 14; b++) {
    input.buttons = 1u << b;
    render();
    check_changes(neutral, regions[b][0], regions[b][1], regions[b][2], regions[b][3]);
  }
  input.buttons = 0;
  const int arrow_regions[4][2] = {{10, 43}, {17, 50}, {10, 57}, {3, 50}};
  for (unsigned i = 0; i < 4; i++) {
    input.directions = 1u << i;
    render();
    check_changes(neutral, arrow_regions[i][0], arrow_regions[i][1], 7, 7);
  }
  input.directions = 0;
  input.report.LX = 0;
  input.report.LY = 0;
  input.report.RX = 255;
  input.report.RY = 255;
  render();
  assert(pixel(guarded.frame, 40, 49) && pixel(guarded.frame, 87, 59));
  input.report.LX = input.report.LY = input.report.RX = input.report.RY = 128;
  render();
  assert(pixel(guarded.frame, 45, 54) && pixel(guarded.frame, 82, 54));
  frame_t usb;
  oled_render_dashboard(usb, &state, &input, true);
  assert(pixel(usb, 123, 3) && !pixel(guarded.frame, 123, 3));
  state.pc_seen = true;
  state.pc_hat = 8;
  state.pc_lx = state.pc_ly = state.pc_rx = state.pc_ry = 128;
  state.pc_age_ms = 59999;
  render();
  frame_t active;
  memcpy(active, guarded.frame, sizeof(active));
  // The centered face stays fixed across modes; source label stays on the left.
  assert(!memcmp(&neutral[0][55], &active[0][55], 17));
  assert(memcmp(neutral[0], active[0], 41));
  assert(pixel(active, 61, 6) && pixel(active, 65, 6));
  // Longer LAST values must stay right-aligned beside ACTIVE, never overlap it.
  const uint32_t ages[] = {10000, 3599999, 3600000, 86399999, 86400000, UINT32_MAX};
  state.pc_buttons = 1;
  for (unsigned i = 0; i < sizeof(ages) / sizeof(ages[0]); ++i) {
    state.pc_age_ms = ages[i];
    render();
    check_changes(active, 75, 19, 53, 7);
  }
  state.pc_buttons = 0;
  state.pc_age_ms = 60000;
  render();
  // Compare only the state text; LAST changes independently of activity.
  assert(memcmp(active[1], guarded.frame[1], 128));
  frame_t idle;
  memcpy(idle, guarded.frame, sizeof(idle));
  state.pc_age_ms = UINT32_MAX;
  for (unsigned button = 0; button < 14; ++button) {
    state.pc_buttons = 1u << button;
    render();
    assert(!memcmp(active[1], guarded.frame[1], 128));
  }
  state.pc_buttons = 0;
  for (unsigned hat = 0; hat < 8; ++hat) {
    state.pc_hat = hat;
    render();
    assert(!memcmp(active[1], guarded.frame[1], 128));
  }
  state.pc_hat = 8;
  uint8_t *axes[] = {&state.pc_lx, &state.pc_ly, &state.pc_rx, &state.pc_ry};
  for (unsigned axis = 0; axis < 4; ++axis) {
    *axes[axis] = 127;
    render();
    assert(!memcmp(active[1], guarded.frame[1], 128));
    *axes[axis] = 129;
    render();
    assert(!memcmp(active[1], guarded.frame[1], 128));
    *axes[axis] = 128;
  }
  render();
  assert(!memcmp(idle[1], guarded.frame[1], 128));
  state = (easycon_script_status_t){.state = EASYCON_SCRIPT_RUNNING, .ecx = true, .pc_seen = true};
  render();
  frame_t running;
  memcpy(running, guarded.frame, sizeof(running));
  state.pc_seen = false;
  render();
  assert(!memcmp(running, guarded.frame, sizeof(running)));
  // Old and ECX interpreters share OFFLINE; a waiting legacy VM is not standby.
  state.ecx = false;
  state.end_address = 4;
  render();
  assert(!memcmp(running[0], guarded.frame[0], 128));
  state.state = EASYCON_SCRIPT_WAITING;
  state.wait_ms = 100;
  render();
  frame_t short_wait;
  memcpy(short_wait, guarded.frame, sizeof(short_wait));
  state.wait_ms = 900;
  render();
  check_changes(short_wait, 87, 11, 41, 7);
  state.wait_ms = 1;
  render();
  assert(!memcmp(short_wait, guarded.frame, sizeof(short_wait)));
  const uint32_t waits[] = {9999, 10000, 59999, 60000, 3599999, 3600000,
                            86399999, 86400000, UINT32_MAX};
  for (unsigned i = 0; i < sizeof(waits) / sizeof(waits[0]); ++i) {
    state.wait_ms = waits[i];
    render();
    check_changes(short_wait, 87, 11, 41, 7);
  }
  for (int s = 0; s <= EASYCON_SCRIPT_ERROR; s++) {
    state.state = s;
    state.elapsed_ms = UINT32_MAX;
    state.wait_ms = UINT32_MAX;
    for (int e = 0; e <= 14; e++) {
      state.error_code = e;
      render();
    }
  }
  state.ecx = false;
  state.state = EASYCON_SCRIPT_FINISHED;
  state.end_address = 0;
  render();
  frame_t empty;
  memcpy(empty, guarded.frame, sizeof(empty));
  state.ecx = true;
  render();
  assert(memcmp(empty, guarded.frame, sizeof(empty)));
  state.state = EASYCON_SCRIPT_ERROR;
  state.error_code = INT_MIN;
  render();
  state.state = (easycon_script_state_t)-1;
  render();
  if (argc == 3 && !strcmp(argv[1], "--preview"))
    preview(argv[2]);
  puts("OLED layout: state modes, input regions, axes, USB and bounds passed");
}
