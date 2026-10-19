#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "easycon.h"
#include "hid_device.h"

static uint8_t storage[1024];
static switch_hid_report_t report;
static int64_t clock_us;
int64_t esp_timer_get_time(void) { return clock_us; }
uint8_t EasyCon_read_byte(uint16_t addr) { assert(addr < sizeof(storage)); return storage[addr]; }
void EasyCon_write_start(uint8_t mode) { (void)mode; }
void EasyCon_write_end(uint8_t mode) { (void)mode; }
void EasyCon_write_data(uint16_t addr, uint8_t *data, uint16_t len) {
    assert(addr + len <= sizeof(storage));
    memcpy(storage + addr, data, len);
}
void uart_handler_send(uint8_t byte) { (void)byte; }
void hid_device_get_report(switch_hid_report_t *r) { *r = report; }
void hid_device_set_report(const switch_hid_report_t *r) { report = *r; }
void hid_device_send(void) {}
int hid_device_is_ready(void) { return 1; }
uint32_t hid_device_get_send_count(void) { return 0; }
uint32_t hid_device_get_fail_count(void) { return 0; }

static easycon_script_status_t snapshot(void) {
    easycon_script_status_t s;
    EasyCon_get_script_status(&s);
    return s;
}
static void advance(unsigned ms) {
    for (unsigned i = 0; i < ms; ++i) {
        clock_us += 1000;
        EasyCon_tick();
        EasyCon_report_send_callback();
    }
}
static void send_pc_report(void) {
    const uint8_t bytes[] = {0, 1, 0, 0, 0, 0, 0, 0x80};
    EasyCon_serial_task(CMD_READY);
    for (unsigned i = 0; i < sizeof(bytes); ++i) EasyCon_serial_task(bytes[i]);
}
int main(void) {
    EasyCon_script_init();
    assert(snapshot().state == EASYCON_SCRIPT_IDLE);
    send_pc_report();
    assert(snapshot().pc_seen && snapshot().pc_reports == 1);
    assert(snapshot().pc_age_ms == 0 && snapshot().pc_buttons == BUTTON_A);
    advance(2000);
    EasyCon_serial_task(CMD_READY);
    EasyCon_serial_task(CMD_HELLO); // Heartbeat must not count as PC activity.
    assert(snapshot().pc_age_ms == 2000 && snapshot().pc_reports == 1);
    send_pc_report();
    assert(snapshot().pc_age_ms == 0 && snapshot().pc_reports == 2);
    EasyCon_serial_task(CMD_READY);
    storage[0] = 4;
    storage[1] = 0;
    // VM wait instruction: 0x0108 * 10 = 2640 ms.
    storage[2] = 0x08;
    storage[3] = 0x01;
    EasyCon_serial_task(CMD_SCRIPTSTART);
    assert(snapshot().state == EASYCON_SCRIPT_RUNNING);
    assert(!snapshot().pc_seen && snapshot().pc_reports == 0);
    send_pc_report(); // Busy VM rejects remote input, so it is not displayed.
    assert(!snapshot().pc_seen);
    EasyCon_serial_task(CMD_READY);
    assert(snapshot().address == 2 && snapshot().end_address == 4);
    EasyCon_script_task();
    advance(200);
    EasyCon_script_task();
    easycon_script_status_t s = snapshot();
    assert(s.state == EASYCON_SCRIPT_WAITING);
    assert(s.address == 4 && s.wait_ms == 2440 && s.elapsed_ms == 200);
    EasyCon_serial_task(CMD_SCRIPTSTOP);
    s = snapshot();
    assert(s.state == EASYCON_SCRIPT_STOPPED && s.wait_ms == 0);
    advance(500);
    assert(snapshot().elapsed_ms == s.elapsed_ms);
    EasyCon_serial_task(CMD_SCRIPTSTART);
    assert(snapshot().elapsed_ms == 0 && snapshot().address == 2);
    EasyCon_script_task();
    advance(2640);
    EasyCon_script_task();
    s = snapshot();
    assert(s.state == EASYCON_SCRIPT_FINISHED && s.elapsed_ms == 2640);
    assert(s.address == s.end_address && s.wait_ms == 0);
    advance(1000);
    assert(snapshot().elapsed_ms == 2640);
    puts("Script status lifecycle passed");
    return 0;
}
