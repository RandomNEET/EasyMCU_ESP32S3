#include "hid_device.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define FEEDBACK_US 200000
static portMUX_TYPE report_mux = portMUX_INITIALIZER_UNLOCKED;
static switch_hid_report_t current = {
    .HAT = SWITCH_HAT_CENTERED,
    .LX = SWITCH_STICK_CENTER, .LY = SWITCH_STICK_CENTER,
    .RX = SWITCH_STICK_CENTER, .RY = SWITCH_STICK_CENTER,
};
static int64_t button_until[14];
static int64_t direction_until[4];

static uint8_t directions(uint8_t hat) {
    // UP, RIGHT, DOWN, LEFT; diagonals highlight both cardinal directions.
    static const uint8_t mask[] = {1, 3, 2, 6, 4, 12, 8, 9, 0};
    return hat <= SWITCH_HAT_CENTERED ? mask[hat] : 0;
}

void hid_device_set_report(const switch_hid_report_t *report) {
    if (!report) return;
    portENTER_CRITICAL(&report_mux);
    int64_t until = esp_timer_get_time() + FEEDBACK_US;
    uint16_t changed = current.Button ^ report->Button;
    for (unsigned i = 0; i < 14; ++i)
        if (changed & (1u << i)) button_until[i] = until;
    uint8_t changed_directions = directions(current.HAT) ^ directions(report->HAT);
    for (unsigned i = 0; i < 4; ++i)
        if (changed_directions & (1u << i)) direction_until[i] = until;
    current = *report;
    portEXIT_CRITICAL(&report_mux);
}

void hid_device_get_report(switch_hid_report_t *report) {
    if (!report) return;
    portENTER_CRITICAL(&report_mux);
    *report = current;
    portEXIT_CRITICAL(&report_mux);
}

void hid_device_get_display_snapshot(hid_display_snapshot_t *snapshot) {
    if (!snapshot) return;
    portENTER_CRITICAL(&report_mux);
    int64_t now = esp_timer_get_time();
    snapshot->report = current;
    snapshot->buttons = current.Button & 0x3fff;
    snapshot->directions = directions(current.HAT);
    for (unsigned i = 0; i < 14; ++i)
        if (now < button_until[i]) snapshot->buttons |= 1u << i;
    for (unsigned i = 0; i < 4; ++i)
        if (now < direction_until[i]) snapshot->directions |= 1u << i;
    portEXIT_CRITICAL(&report_mux);
}
