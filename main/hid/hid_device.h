#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SWITCH_BUTTON_Y (1 << 0)
#define SWITCH_BUTTON_B (1 << 1)
#define SWITCH_BUTTON_A (1 << 2)
#define SWITCH_BUTTON_X (1 << 3)
#define SWITCH_BUTTON_L (1 << 4)
#define SWITCH_BUTTON_R (1 << 5)
#define SWITCH_BUTTON_ZL (1 << 6)
#define SWITCH_BUTTON_ZR (1 << 7)
#define SWITCH_BUTTON_MINUS (1 << 8)
#define SWITCH_BUTTON_PLUS (1 << 9)
#define SWITCH_BUTTON_LCLICK (1 << 10)
#define SWITCH_BUTTON_RCLICK (1 << 11)
#define SWITCH_BUTTON_HOME (1 << 12)
#define SWITCH_BUTTON_CAPTURE (1 << 13)

#define SWITCH_HAT_TOP 0
#define SWITCH_HAT_TOP_RIGHT 1
#define SWITCH_HAT_RIGHT 2
#define SWITCH_HAT_BOTTOM_RIGHT 3
#define SWITCH_HAT_BOTTOM 4
#define SWITCH_HAT_BOTTOM_LEFT 5
#define SWITCH_HAT_LEFT 6
#define SWITCH_HAT_TOP_LEFT 7
#define SWITCH_HAT_CENTERED 8

#define SWITCH_STICK_MIN 0
#define SWITCH_STICK_CENTER 128
#define SWITCH_STICK_MAX 255

typedef struct {
  uint16_t Button;
  uint8_t HAT;
  uint8_t LX;
  uint8_t LY;
  uint8_t RX;
  uint8_t RY;
  uint8_t VendorSpec;
} __attribute__((packed)) switch_hid_report_t;

// Display feedback is independent of the report actually sent over USB.
// buttons/directions include 200 ms of recent changes; axes are instantaneous.
typedef struct {
  switch_hid_report_t report;
  uint16_t buttons;
  uint8_t directions; // bit 0=up, 1=right, 2=down, 3=left
} hid_display_snapshot_t;

void hid_device_get_display_snapshot(hid_display_snapshot_t *snapshot);
void hid_device_init(void);
void hid_device_set_report(const switch_hid_report_t *report);
void hid_device_get_report(switch_hid_report_t *report);
void hid_device_send(void);
int hid_device_is_ready(void);
// Enumerated and not suspended; this does not measure physical VBUS presence.
int hid_device_is_usb_active(void);
uint32_t hid_device_get_send_count(void);
uint32_t hid_device_get_fail_count(void);
