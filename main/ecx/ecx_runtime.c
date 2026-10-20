#include "ecx_runtime.h"
#include "vendor/ecs_vm.h"
#include "hid_device.h"
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "ecx";
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static nvs_handle_t store;
static bool store_ready;
// Only the script task owns VM allocation/free/run. Transport only requests.
static ecs_vm *vm;
static uint8_t *saved, *staging;
static size_t saved_size, stage_size, stage_received;
static uint32_t stage_crc;
static bool stage_auto, pending, active, cancel, updating, session;
static easycon_script_status_t state;
static int64_t started, deadline;
static uint8_t rx[3 + ECX_MAX_PAYLOAD + 4];
static size_t rx_size, rx_expected;
static int64_t rx_last;

static uint32_t le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t crc = UINT32_MAX;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = n >> (8 * i);
}
static bool cancelled(void) {
    portENTER_CRITICAL(&mux);
    bool result = cancel;
    portEXIT_CRITICAL(&mux);
    return result;
}
static void host_wait(void *ud, int32_t ms) {
    (void)ud;
    if (ms <= 0) return;
    int64_t until = esp_timer_get_time() + (int64_t)ms * 1000;
    portENTER_CRITICAL(&mux);
    state.state = EASYCON_SCRIPT_WAITING;
    deadline = until;
    portEXIT_CRITICAL(&mux);
    while (!cancelled() && esp_timer_get_time() < until)
        vTaskDelay(pdMS_TO_TICKS(1));
    portENTER_CRITICAL(&mux);
    deadline = 0;
    state.state = EASYCON_SCRIPT_RUNNING;
    portEXIT_CRITICAL(&mux);
}
static void host_key_state(void *ud, uint8_t key, int down) {
    (void)ud;
    if (cancelled()) return;
    if (key < 14) {
        if (down) press_buttons(1u << key);
        else release_buttons(1u << key);
    } else if (key >= 16 && key <= 23) {
        set_HAT_switch(down ? key & 0xf : HAT_CENTERED);
    }
}
static void host_key(void *ud, uint8_t key, int32_t ms) {
    host_key_state(ud, key, 1);
    host_wait(ud, ms);
    host_key_state(ud, key, 0);
}
static uint8_t clamp_axis(int32_t n) { return n < 0 ? 0 : n > 255 ? 255 : n; }
static void host_stick(void *ud, uint8_t side, int32_t x, int32_t y) {
    (void)ud;
    if (cancelled()) return;
    if (side == 0) set_left_stick(clamp_axis(x), clamp_axis(y));
    else if (side == 1) set_right_stick(clamp_axis(x), clamp_axis(y));
}
static void host_stick_click(void *ud, uint8_t side, int32_t x, int32_t y, int32_t ms) {
    host_stick(ud, side, x, y);
    host_wait(ud, ms);
    host_stick(ud, side, STICK_CENTER, STICK_CENTER);
}
static int32_t host_rand(void *ud, int32_t max) {
    (void)ud;
    return max > 0 ? (int32_t)(esp_random() % (uint32_t)max) : 0;
}
static int host_syscall(void *ud, int32_t id, ecs_value *args, int32_t nargs, ecs_value *ret) {
    (void)ud;
    *ret = (ecs_value){0};
    switch (id) {
    case ECS_SYSCALL_FWRITE:
        // PRINT is accepted silently; never inject text into the control port.
        if (nargs < 2) return 1;
        ret->tag = ECS_INT;
        const uint16_t *units;
        ret->i32 = args[0].i64 <= 2 ? ecs_vm_arg_str(vm, args[1], &units) : -1;
        return 0;
    case ECS_SYSCALL_TIME:
        ret->tag = ECS_INT;
        ret->i32 = (int32_t)((esp_timer_get_time() - started) / 1000);
        return 0;
    case ECS_SYSCALL_ALERT: case ECS_SYSCALL_BEEP: case ECS_SYSCALL_AMIIBO:
        return 0;
    default: return 1;
    }
}
static ecs_host host(void) {
    return (ecs_host){.feats = ECS_FEAT_FILE, .wait_ms = host_wait, .key = host_key, .key_state = host_key_state,
        .stick_set = host_stick, .stick_click = host_stick_click, .rand = host_rand,
        .syscall = host_syscall};
}
static int validate(const uint8_t *image, size_t size) {
    ecs_host h = host();
    ecs_vm *candidate = ecs_vm_new(&h);
    if (!candidate) return ECS_ERR_OOM;
    int result = ecs_vm_load(candidate, image, size);
    // FILE is also set by PRINT/FWRITE. Permit that flag but reject actual
    // filesystem calls before saving or executing any part of the image.
    if (result == ECS_OK)
        result = ecs_vm_check_syscalls(candidate, (1u << ECS_SYSCALL_FWRITE) |
            (1u << ECS_SYSCALL_TIME) | (1u << ECS_SYSCALL_ALERT) |
            (1u << ECS_SYSCALL_BEEP) | (1u << ECS_SYSCALL_AMIIBO));
    ecs_vm_free(candidate);
    return result;
}
esp_err_t ecx_runtime_init(void) {
    esp_err_t err = nvs_flash_init_partition("storage");
    if (err != ESP_OK) return err;
    err = nvs_open_from_partition("storage", "ecx", NVS_READWRITE, &store);
    if (err != ESP_OK) return err;
    store_ready = true;
    size_t size = 0;
    err = nvs_get_blob(store, "program", NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK || size < 16 + 36 || size > 16 + ECX_MAX_IMAGE) return ESP_ERR_INVALID_SIZE;
    uint8_t *blob = malloc(size);
    if (!blob) return ESP_ERR_NO_MEM;
    err = nvs_get_blob(store, "program", blob, &size);
    if (err == ESP_OK && (memcmp(blob, "ECF2", 4) || le32(blob + 4) != size - 16 ||
        crc32(blob + 16, size - 16) != le32(blob + 8) || validate(blob + 16, size - 16) != ECS_OK))
        err = ESP_ERR_INVALID_CRC;
    if (err != ESP_OK) { free(blob); return err; }
    saved = blob;
    saved_size = size;
    return ESP_OK;
}
bool ecx_present(void) {
    portENTER_CRITICAL(&mux);
    bool present = saved != NULL;
    portEXIT_CRITICAL(&mux);
    return present;
}
bool ecx_busy(void) {
    portENTER_CRITICAL(&mux);
    bool busy = pending || active || updating;
    portEXIT_CRITICAL(&mux);
    return busy;
}
bool ecx_start(void) {
    portENTER_CRITICAL(&mux);
    bool ok = saved != NULL && !pending && !active && !updating;
    if (ok) {
        pending = session = true;
        cancel = false;
        started = esp_timer_get_time();
        state = (easycon_script_status_t){.state = EASYCON_SCRIPT_RUNNING, .ecx = true};
    }
    portEXIT_CRITICAL(&mux);
    return ok;
}
void ecx_stop(void) {
    portENTER_CRITICAL(&mux);
    cancel = true;
    portEXIT_CRITICAL(&mux);
}
void ecx_status(easycon_script_status_t *out) {
    portENTER_CRITICAL(&mux);
    if (session) {
        out->state = state.state;
        out->ecx = true;
        out->error_code = state.error_code;
        out->elapsed_ms = state.elapsed_ms;
        if (pending || active) out->elapsed_ms = (uint32_t)((esp_timer_get_time() - started) / 1000);
        int64_t remaining = deadline - esp_timer_get_time();
        out->wait_ms = remaining > 0 ? (uint32_t)(remaining / 1000) : 0;
        out->address = out->end_address = 0;
    }
    portEXIT_CRITICAL(&mux);
}
static void finish(int result) {
    reset_hid_report();
    ecs_vm_free(vm);
    vm = NULL;
    portENTER_CRITICAL(&mux);
    state.elapsed_ms = (uint32_t)((esp_timer_get_time() - started) / 1000);
    state.error_code = result;
    state.state = result == ECS_OK ? EASYCON_SCRIPT_FINISHED :
                  result == ECS_CANCELLED ? EASYCON_SCRIPT_STOPPED : EASYCON_SCRIPT_ERROR;
    deadline = 0;
    active = pending = false;
    portEXIT_CRITICAL(&mux);
    if (result != ECS_OK && result != ECS_CANCELLED) ESP_LOGE(TAG, "VM error %d", result);
}
void ecx_poll(void) {
    portENTER_CRITICAL(&mux);
    bool begin = pending;
    if (begin) { pending = false; active = true; }
    bool running = active;
    portEXIT_CRITICAL(&mux);
    if (!running) return;
    if (cancelled()) { finish(ECS_CANCELLED); return; }
    if (begin) {
        ecs_host h = host();
        vm = ecs_vm_new(&h);
        if (!vm) { finish(ECS_ERR_OOM); return; }
        int result = ecs_vm_load(vm, saved + 16, saved_size - 16);
        if (result != ECS_OK) { finish(result); return; }
        ecs_vm_set_budget(vm, 1000);
    }
    int result = ecs_vm_run(vm);
    if (cancelled()) result = ECS_CANCELLED;
    if (result != ECS_YIELD) finish(result);
}

// BEGIN(size:u32,crc:u32,auto:u8), DATA(offset:u32,bytes), COMMIT,
// START, STOP, CLEAR. Frame = op:u8,len:u16,payload,crc32(header+payload).
static uint8_t action(uint8_t op, const uint8_t *data, size_t size) {
    if (op == 4 && size == 0) { EasyCon_script_stop(); return REPLY_ACK; }
    if (ecx_busy()) return REPLY_BUSY;
    if (op == 0 && size == 9) {
        size_t length = le32(data);
        if (length < 36 || length > ECX_MAX_IMAGE || data[8] > 1) return REPLY_ERROR;
        uint8_t *buffer = malloc(length + 16);
        if (!buffer) return REPLY_ERROR;
        free(staging);
        staging = buffer;
        stage_size = length;
        stage_received = 0;
        stage_crc = le32(data + 4);
        stage_auto = data[8];
        return REPLY_ACK;
    }
    if (op == 1 && size > 4 && staging) {
        uint32_t offset = le32(data);
        size_t count = size - 4;
        if (offset > stage_received || count > stage_size || offset > stage_size - count) return REPLY_ERROR;
        if (offset < stage_received)
            return offset + count <= stage_received && !memcmp(staging + 16 + offset, data + 4, count) ? REPLY_ACK : REPLY_ERROR;
        memcpy(staging + 16 + offset, data + 4, count);
        stage_received += count;
        return REPLY_ACK;
    }
    if (op == 2 && size == 0 && staging && stage_received == stage_size && store_ready) {
        portENTER_CRITICAL(&mux);
        updating = true;
        portEXIT_CRITICAL(&mux);
        int validation = validate(staging + 16, stage_size);
        if (crc32(staging + 16, stage_size) != stage_crc || validation != ECS_OK) {
            ESP_LOGE(TAG, "Image rejected, VM result %d", validation);
            portENTER_CRITICAL(&mux);
            updating = false;
            portEXIT_CRITICAL(&mux);
            return REPLY_ERROR;
        }
        memcpy(staging, "ECF2", 4);
        put32(staging + 4, stage_size);
        put32(staging + 8, stage_crc);
        memset(staging + 12, 0, 4);
        staging[12] = stage_auto;
        portENTER_CRITICAL(&mux);
        updating = true;
        portEXIT_CRITICAL(&mux);
        esp_err_t err = nvs_set_blob(store, "program", staging, stage_size + 16);
        if (err == ESP_OK) err = nvs_commit(store);
        uint8_t *previous = NULL;
        portENTER_CRITICAL(&mux);
        if (err == ESP_OK) {
            previous = saved;
            saved = staging;
            saved_size = stage_size + 16;
            staging = NULL;
            session = false;
        }
        updating = false;
        portEXIT_CRITICAL(&mux);
        free(previous);
        return err == ESP_OK ? REPLY_ACK : REPLY_ERROR;
    }
    if (op == 3 && size == 0 && ecx_present()) {
        EasyCon_script_start();
        return ecx_busy() ? REPLY_ACK : REPLY_ERROR;
    }
    if (op == 5 && size == 0 && store_ready) {
        portENTER_CRITICAL(&mux);
        updating = true;
        portEXIT_CRITICAL(&mux);
        esp_err_t err = nvs_erase_key(store, "program");
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err == ESP_OK) err = nvs_commit(store);
        uint8_t *previous = NULL;
        portENTER_CRITICAL(&mux);
        if (err == ESP_OK) {
            previous = saved; saved = NULL; saved_size = 0;
            session = false;
        }
        updating = false;
        portEXIT_CRITICAL(&mux);
        free(previous);
        if (err == ESP_OK) { free(staging); staging = NULL; }
        return err == ESP_OK ? REPLY_ACK : REPLY_ERROR;
    }
    return REPLY_ERROR;
}
void ecx_rx_begin(void) {
    rx_size = 0; rx_expected = 3;
    rx_last = esp_timer_get_time();
}
bool ecx_rx_byte(uint8_t byte) {
    if (rx_expected == 0) return false;
    if (esp_timer_get_time() - rx_last > 1000000) {
        rx_expected = 0;
        return false;
    }
    rx_last = esp_timer_get_time();
    rx[rx_size++] = byte;
    if (rx_size == 3) {
        size_t length = rx[1] | ((size_t)rx[2] << 8);
        if (length > ECX_MAX_PAYLOAD) { rx_expected = 0; EasyCon_serial_send(REPLY_ERROR); return true; }
        rx_expected = 3 + length + 4;
    }
    if (rx_size == rx_expected) {
        rx_expected = 0;
        size_t size = rx_size - 4;
        uint8_t result = crc32(rx, size) == le32(rx + size) ? action(rx[0], rx + 3, size - 3) : REPLY_ERROR;
        EasyCon_serial_send(result);
    }
    return true;
}
bool ecx_auto_run(void) {
    portENTER_CRITICAL(&mux);
    bool auto_run = saved && saved[12] != 0;
    portEXIT_CRITICAL(&mux);
    return auto_run;
}
