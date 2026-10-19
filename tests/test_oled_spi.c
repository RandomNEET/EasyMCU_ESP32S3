#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oled_status.h"
#include "easycon.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

static bool dc, display_on, task_started;
static unsigned pages, data_bytes, chunks, cleanup_count;
static int fail_at_page = -1;
static uint8_t pixels[1024];
static easycon_script_status_t mock_status;
const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }
esp_err_t gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
esp_err_t gpio_set_level(int pin, unsigned level) { if (pin == 9) dc = level; return ESP_OK; }
esp_err_t spi_bus_initialize(int host, const spi_bus_config_t *config, int dma) {
    assert(host == SPI2_HOST && dma == SPI_DMA_DISABLED);
    assert(config->max_transfer_sz <= 64);
    return ESP_OK;
}
esp_err_t spi_bus_add_device(int host, const spi_device_interface_config_t *config, spi_device_handle_t *device) {
    (void)host;
    assert(config->spics_io_num == 10 && config->mode == 0);
    *device = (void *)1;
    return ESP_OK;
}
esp_err_t spi_device_transmit(spi_device_handle_t device, spi_transaction_t *transaction) {
    assert(device);
    // Match the real ESP32-S3 driver's non-DMA limit. The old 128-byte
    // page transfer fails here before the display-on command is reached.
    if (transaction->length > 64 * 8) return ESP_ERR_INVALID_ARG;
    const uint8_t *bytes = transaction->tx_buffer;
    size_t size = transaction->length / 8;
    if (dc) {
        if ((int)pages - 1 == fail_at_page) return ESP_ERR_INVALID_ARG;
        assert(size == 64 && data_bytes + size <= sizeof(pixels));
        memcpy(pixels + data_bytes, bytes, size);
        data_bytes += size;
        ++chunks;
    } else if (size == 3) {
        assert(bytes[0] == (0xb0 | pages));
        assert(bytes[1] == 2 && bytes[2] == 0x10);
        assert(data_bytes == pages * 128);
        ++pages;
    } else if (size == 1 && bytes[0] == 0xaf) {
        assert(pages == 8 && data_bytes == 1024);
        display_on = true;
    }
    return ESP_OK;
}
esp_err_t spi_bus_remove_device(spi_device_handle_t device) { assert(device); ++cleanup_count; return ESP_OK; }
esp_err_t spi_bus_free(int host) { (void)host; ++cleanup_count; return ESP_OK; }
void vTaskDelay(unsigned ticks) { (void)ticks; }
void vTaskDelete(void *task) { (void)task; }
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle) {
    (void)entry; (void)name; (void)stack; (void)arg; (void)priority; (void)handle;
    assert(display_on);
    task_started = true;
    return 1;
}
void EasyCon_get_script_status(easycon_script_status_t *status) {
    *status = mock_status;
}
int hid_device_is_mounted(void) { return 0; }
int main(void) {
    assert(oled_status_init() == ESP_OK);
    assert(display_on && task_started && chunks == 16);
    assert(pixels[0] != 0); // First glyph reached the framebuffer.
    // PC state changes at the 2-second boundary; assert rendered glyphs.
    mock_status.pc_seen = true;
    mock_status.pc_age_ms = 1999;
    pages = data_bytes = chunks = 0;
    display_on = task_started = false;
    assert(oled_status_init() == ESP_OK);
    assert(pixels[2 * 128] == 0x7f && pixels[2 * 128 + 1] == 0x09); // P
    assert(pixels[2 * 128 + 18] == 0x7e); // A in PC ACTIVE
    mock_status.pc_age_ms = 2000;
    pages = data_bytes = chunks = 0;
    display_on = task_started = false;
    assert(oled_status_init() == ESP_OK);
    assert(pixels[2 * 128 + 18] == 0x00 && pixels[2 * 128 + 19] == 0x41); // I
    // An empty onboard script must not be shown as a completed PC script.
    mock_status = (easycon_script_status_t){.state = EASYCON_SCRIPT_FINISHED};
    pages = data_bytes = chunks = 0;
    display_on = task_started = false;
    assert(oled_status_init() == ESP_OK);
    assert(pixels[2 * 128] == 0x7f && pixels[2 * 128 + 1] == 0x04); // N
    pages = data_bytes = chunks = 0;
    display_on = task_started = false;
    fail_at_page = 0;
    assert(oled_status_init() == ESP_ERR_INVALID_ARG);
    assert(!display_on && !task_started && cleanup_count == 2);
    puts("OLED FIFO limit, page data, display-on and failure cleanup passed");
}
