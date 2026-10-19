#include "oled_status.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "easycon.h"
#include "hid_device.h"

// Four-wire SPI pinout from doc/sheets.md. SH1106 has 132 RAM columns;
// this module's 128 visible columns start at column 2.
#define OLED_SCLK 12
#define OLED_MOSI 11
#define OLED_RESET 46
#define OLED_DC 9
#define OLED_CS 10
#define OLED_WIDTH 128
#define OLED_PAGES 8
#define OLED_COLUMN_OFFSET 2
// Non-DMA SPI uses a 64-byte hardware FIFO on ESP32-S3.
#define OLED_TRANSFER_BYTES 64

static const char *TAG = "oled";
static spi_device_handle_t oled;
static uint8_t frame[OLED_PAGES][OLED_WIDTH];

// Original 5x7 column glyphs for the status UI (LSB at top).
static const char glyph_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-/";
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
};
_Static_assert(sizeof(glyphs) / sizeof(glyphs[0]) == sizeof(glyph_chars) - 1,
               "Font mapping mismatch");

static void text(unsigned page, const char *str) {
    unsigned x = 0;
    while (*str && x + 6 <= OLED_WIDTH && page < OLED_PAGES) {
        const char *g = strchr(glyph_chars, *str++);
        if (g) memcpy(&frame[page][x], glyphs[g - glyph_chars], 5);
        x += 6;
    }
}

static esp_err_t transfer(bool data, const uint8_t *bytes, size_t length) {
    esp_err_t err = gpio_set_level(OLED_DC, data);
    if (err != ESP_OK) return err;
    // CS may toggle between chunks: SH1106 retains its page/column pointer.
    while (length > 0) {
        size_t chunk = length > OLED_TRANSFER_BYTES ? OLED_TRANSFER_BYTES : length;
        spi_transaction_t transaction = {
            .length = chunk * 8,
            .tx_buffer = bytes,
        };
        err = spi_device_transmit(oled, &transaction);
        if (err != ESP_OK) return err;
        bytes += chunk;
        length -= chunk;
    }
    return ESP_OK;
}

static esp_err_t refresh(void) {
    for (unsigned page = 0; page < OLED_PAGES; ++page) {
        const uint8_t address[] = {0xb0 | page,
            OLED_COLUMN_OFFSET & 0x0f, 0x10 | (OLED_COLUMN_OFFSET >> 4)};
        esp_err_t err = transfer(false, address, sizeof(address));
        if (err != ESP_OK) return err;
        err = transfer(true, frame[page], OLED_WIDTH);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static void render(void) {
    static const char *const states[] = {"IDLE", "RUNNING", "WAITING", "STOPPED", "FINISHED"};
    easycon_script_status_t status;
    EasyCon_get_script_status(&status);
    char line[32];
    memset(frame, 0, sizeof(frame));
    text(0, "EASYMCU / SH1106");
    bool onboard_running = status.state == EASYCON_SCRIPT_RUNNING ||
                           status.state == EASYCON_SCRIPT_WAITING;
    if (status.pc_seen && !onboard_running) {
        text(2, status.pc_age_ms < 2000 ? "PC ACTIVE" : "PC IDLE");
        snprintf(line, sizeof(line), "LAST %" PRIu32 " MS", status.pc_age_ms);
        text(3, line);
        snprintf(line, sizeof(line), "REPORTS %" PRIu32, status.pc_reports);
        text(4, line);
        snprintf(line, sizeof(line), "BTN %04X HAT %X", (unsigned)status.pc_buttons,
                 (unsigned)status.pc_hat);
        text(5, line);
        snprintf(line, sizeof(line), "L %02X %02X R %02X %02X", status.pc_lx,
                 status.pc_ly, status.pc_rx, status.pc_ry);
        text(6, line);
    } else {
        text(2, status.state == EASYCON_SCRIPT_FINISHED && status.end_address <= 2
                ? "NO SCRIPT" : states[status.state]);
        uint32_t seconds = status.elapsed_ms / 1000;
        snprintf(line, sizeof(line), "TIME %02" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
                 seconds / 3600, (seconds / 60) % 60, seconds % 60);
        text(3, line);
        snprintf(line, sizeof(line), "ADDR %04X END %04X", (unsigned)status.address,
                 (unsigned)status.end_address);
        text(4, line);
        snprintf(line, sizeof(line), "WAIT %" PRIu32 " MS", status.wait_ms);
        text(5, line);
    }
    text(7, hid_device_is_mounted() ? "USB CONNECTED" : "USB DISCONNECTED");
}

static void display_task(void *arg) {
    (void)arg;
    for (;;) {
        render();
        esp_err_t err = refresh();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Display update failed: %s", esp_err_to_name(err));
            // This task owns SPI2; release its resources if it stops.
            spi_bus_remove_device(oled);
            oled = NULL;
            spi_bus_free(SPI2_HOST);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

esp_err_t oled_status_init(void) {
    const gpio_config_t pins = {
        .pin_bit_mask = (1ULL << OLED_DC) | (1ULL << OLED_RESET),
        .mode = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&pins);
    if (err != ESP_OK) return err;
    const spi_bus_config_t bus = {
        .mosi_io_num = OLED_MOSI, .miso_io_num = -1, .sclk_io_num = OLED_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = OLED_TRANSFER_BYTES,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_DISABLED);
    if (err != ESP_OK) return err;
    const spi_device_interface_config_t device = {
        .clock_speed_hz = 1000000, .mode = 0,
        .spics_io_num = OLED_CS, .queue_size = 1,
    };
    err = spi_bus_add_device(SPI2_HOST, &device, &oled);
    if (err != ESP_OK) {
        spi_bus_free(SPI2_HOST);
        return err;
    }
    gpio_set_level(OLED_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(OLED_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(OLED_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    // SH1106 internal DC/DC, 1/64 duty, page addressing. Do not use
    // SSD1306 horizontal addressing or its 0x8d charge-pump command.
    const uint8_t init[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
        0xad, 0x8b, 0xa1, 0xc8, 0xda, 0x12, 0x81, 0x80,
        0xd9, 0x22, 0xdb, 0x35, 0xa4, 0xa6,
    };
    err = transfer(false, init, sizeof(init));
    if (err == ESP_OK) {
        render();
        err = refresh();
    }
    const uint8_t on = 0xaf;
    if (err == ESP_OK) err = transfer(false, &on, 1);
    if (err == ESP_OK && xTaskCreate(display_task, "oled_status", 3072, NULL, 1, NULL) != pdPASS)
        err = ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        spi_bus_remove_device(oled);
        oled = NULL;
        spi_bus_free(SPI2_HOST);
    }
    return err;
}
