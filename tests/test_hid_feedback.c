#include "hid_device.h"
#include <assert.h>
#include <stdio.h>
static int64_t now;
int64_t esp_timer_get_time(void) { return now; }
static hid_display_snapshot_t snapshot(void) {
  hid_display_snapshot_t s;
  hid_device_get_display_snapshot(&s);
  return s;
}
int main(void) {
  switch_hid_report_t neutral = {.HAT = 8, .LX = 128, .LY = 128, .RX = 128, .RY = 128};
  hid_device_set_report(&neutral);
  assert(!snapshot().buttons && !snapshot().directions);
  // Each of 14 keys: fast press/release between screen frames must survive.
  for (unsigned i = 0; i < 14; ++i) {
    switch_hid_report_t r = neutral;
    r.Button = 1u << i;
    hid_device_set_report(&r);
    assert(snapshot().buttons == (1u << i));
    now += 500000; // Held past expiry remains highlighted.
    assert(snapshot().buttons == (1u << i));
    hid_device_set_report(&neutral);
    switch_hid_report_t actual;
    hid_device_get_report(&actual);
    assert(actual.Button == 0); // Visual hold never delays the real release.
    now += 199999;
    assert(snapshot().buttons == (1u << i));
    ++now;
    assert(snapshot().buttons == 0);
  }
  static const uint8_t masks[] = {1, 3, 2, 6, 4, 12, 8, 9, 0};
  for (unsigned hat = 0; hat < 9; hat++) {
    switch_hid_report_t r = neutral;
    r.HAT = hat;
    hid_device_set_report(&r);
    assert(snapshot().directions == masks[hat]);
    hid_device_set_report(&neutral);
    assert(snapshot().directions == masks[hat]);
    now += 200000;
    assert(snapshot().directions == 0);
  }
  switch_hid_report_t r = neutral;
  r.HAT = 0;
  hid_device_set_report(&r);
  r.HAT = 4;
  hid_device_set_report(&r);
  assert(snapshot().directions == 5); // Recent up and current down.
  now += 200000;
  assert(snapshot().directions == 4);
  r.HAT = 255;
  r.LX = 0;
  r.LY = 255;
  r.RX = 255;
  r.RY = 0;
  hid_device_set_report(&r);
  assert(snapshot().report.LX == 0 && snapshot().report.LY == 255);
  r = neutral;
  hid_device_set_report(&r);
  assert(snapshot().report.LX == 128 && snapshot().report.RX == 128);
  now += 200000;
  assert(snapshot().directions == 0);
  puts("HID feedback: 14 keys, hats, 200 ms expiry and immediate release passed");
}
