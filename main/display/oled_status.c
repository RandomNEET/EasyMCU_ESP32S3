#include "oled_status.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "easycon.h"
#include "hid_device.h"
#include "oled_ui.h"

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
    easycon_script_status_t status;
    hid_display_snapshot_t input;
    EasyCon_get_script_status(&status);
    hid_device_get_display_snapshot(&input);
    oled_ui_render(frame, &status, &input, hid_device_is_mounted());
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
        vTaskDelay(pdMS_TO_TICKS(100));
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
