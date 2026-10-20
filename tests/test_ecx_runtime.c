#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ecx_runtime.h"
#include "vendor/ecs_vm.h"
#include "hid_device.h"
#include "nvs.h"

static int64_t now;
static switch_hid_report_t report;
static uint8_t reply, stored[ECX_MAX_IMAGE + 16];
static size_t stored_size;
static bool transport;
static bool stop_during_wait, saw_wait, saw_press, fail_save;
static uint32_t read32(const uint8_t *p) { return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static void put32(uint8_t *p, uint32_t n) { for (int i=0;i<4;i++) p[i] = n >> (i*8); }
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c=~0u;
    while(n--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1)^(0xedb88320u & (0u-(c&1))); }
    return ~c;
}
int64_t esp_timer_get_time(void) { return now; }
uint32_t esp_random(void) { return 7; }
void uart_handler_send(uint8_t byte) { reply = byte; if (transport) { putchar(byte); fflush(stdout); } }
void hid_device_get_report(switch_hid_report_t *r) { *r = report; }
void hid_device_set_report(const switch_hid_report_t *r) { report = *r; if (r->Button & BUTTON_A) saw_press = true; }
void hid_device_send(void) {}
int hid_device_is_ready(void) { return 1; }
uint32_t hid_device_get_send_count(void) { return 0; }
uint32_t hid_device_get_fail_count(void) { return 0; }
uint8_t EasyCon_read_byte(uint16_t addr) { (void)addr; return 255; }
void EasyCon_write_start(uint8_t mode) { (void)mode; }
void EasyCon_write_end(uint8_t mode) { (void)mode; }
void EasyCon_write_data(uint16_t addr, uint8_t *data, uint16_t len) { (void)addr; (void)data; (void)len; }
void vTaskDelay(unsigned ticks) {
    now += ticks * 1000;
    easycon_script_status_t s;
    EasyCon_get_script_status(&s);
    assert(s.ecx && s.state == EASYCON_SCRIPT_WAITING);
    saw_wait = true;
    if (stop_during_wait) EasyCon_script_stop();
}
esp_err_t nvs_flash_init_partition(const char *name) { assert(!strcmp(name,"storage")); return ESP_OK; }
esp_err_t nvs_open_from_partition(const char *p, const char *n, int mode, nvs_handle_t *h) {
    (void)p; (void)n; (void)mode; *h=1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *size) {
    (void)h; (void)key;
    if (!stored_size) return ESP_ERR_NVS_NOT_FOUND;
    if (data) { assert(*size >= stored_size); memcpy(data,stored,stored_size); }
    *size=stored_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size) {
    (void)h; (void)key;
    if (fail_save) return ESP_ERR_NO_MEM;
    assert(size<=sizeof(stored)); memcpy(stored,data,size); stored_size=size; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) { (void)h; (void)key; stored_size=0; return ESP_OK; }
static uint8_t command(unsigned op, const uint8_t *data, size_t size, bool corrupt) {
    uint8_t frame[3+ECX_MAX_PAYLOAD+4];
    frame[0]=op; frame[1]=size; frame[2]=size>>8;
    if (size) memcpy(frame+3,data,size);
    put32(frame+3+size,crc32(frame,3+size) ^ corrupt);
    EasyCon_serial_task(CMD_READY); EasyCon_serial_task(CMD_ECX);
    reply=0x12;
    for(size_t i=0;i<size+7;i++) EasyCon_serial_task(frame[i]);
    assert(reply!=0x12);
    return reply;
}
static void make_image(uint8_t *image, const uint32_t *code, size_t words) {
    memset(image,0,256);
    memcpy(image,"EXC2\2",5); image[8]=2; image[9]=4;
    put32(image+28,1); // function count
    image[37]=2; put32(image+43,words); // function slots and code size
    for(size_t i=0;i<words;i++) put32(image+47+4*i,code[i]);
    put32(image+47+4*words,0); // entry function
}
static void upload(const uint8_t *image, size_t size, bool auto_run) {
    uint8_t begin[9], chunk[260];
    put32(begin,size); put32(begin+4,crc32(image,size)); begin[8]=auto_run;
    assert(command(0,begin,9,false)==REPLY_ACK);
    assert(command(2,NULL,0,false)==REPLY_ERROR); // incomplete upload
    for(size_t off=0;off<size;off+=256) {
        size_t n=size-off>256?256:size-off;
        put32(chunk,off); memcpy(chunk+4,image+off,n);
        assert(command(1,chunk,n+4,true)==REPLY_ERROR); // bad frame CRC
        assert(command(1,chunk,n+4,false)==REPLY_ACK);
        assert(command(1,chunk,n+4,false)==REPLY_ACK); // retry is idempotent
    }
}
static easycon_script_status_t status(void) { easycon_script_status_t s; EasyCon_get_script_status(&s); return s; }
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--boot")) {
        FILE *file = fopen(argv[2], "rb");
        assert(file);
        stored_size = fread(stored, 1, sizeof(stored), file);
        fclose(file);
        assert(ecx_runtime_init() == ESP_OK && ecx_present() && ecx_auto_run());
        EasyCon_script_init();
        EasyCon_script_auto_start();
        for (unsigned i = 0; ecx_busy() && i < 1000; ++i) EasyCon_script_task();
        assert(!ecx_busy() && status().state == EASYCON_SCRIPT_FINISHED);
        assert(command(5, NULL, 0, false) == REPLY_ACK);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--transport")) {
        transport = true;
        assert(ecx_runtime_init() == ESP_OK);
        EasyCon_script_init();
        int byte;
        while ((byte = getchar()) != EOF) { EasyCon_serial_task(byte); ecx_poll(); }
        return 0;
    }
    assert(ecx_runtime_init()==ESP_OK && !ecx_present());
    EasyCon_script_init();
    uint8_t image[256];
    const uint32_t code[]={OP_KeyI | (2u<<8) | (50u<<16), OP_WaitI | (100u<<16), OP_Halt};
    make_image(image,code,3);
    ecs_vm *check = ecs_vm_new(NULL);
    int checked = ecs_vm_load(check, image, 63);
    assert(checked == ECS_OK);
    ecs_vm_free(check);
    upload(image,63,true);
    assert(command(2,NULL,0,false)==REPLY_ACK);
    assert(ecx_present() && ecx_auto_run() && stored_size==79);
    assert(read32(stored+8)==crc32(image,63) && !memcmp(stored+16,image,63));
    EasyCon_script_auto_start();
    assert(ecx_busy());
    assert(command(5,NULL,0,false)==REPLY_BUSY);
    EasyCon_script_task();
    assert(saw_wait && saw_press && !ecx_busy());
    assert(status().state==EASYCON_SCRIPT_FINISHED && status().elapsed_ms==150 && report.Button==0);
    assert(command(3,NULL,0,false)==REPLY_ACK);
    stop_during_wait=true;
    EasyCon_script_task();
    assert(status().state==EASYCON_SCRIPT_STOPPED && report.Button==0 && !ecx_busy());
    stop_during_wait=false;
    // Rejected image and failed storage write must preserve the installed program.
    image[0]='X'; upload(image,63,false);
    assert(command(2,NULL,0,false)==REPLY_ERROR && ecx_auto_run());
    image[0]='E'; image[10]=ECS_FEAT_IL; upload(image,63,false);
    assert(command(2,NULL,0,false)==REPLY_ERROR && ecx_auto_run());
    image[10]=0; upload(image,63,false); fail_save=true;
    assert(command(2,NULL,0,false)==REPLY_ERROR && ecx_auto_run()); fail_save=false;
    const uint32_t badcode[]={OP_LoadI | (1u<<16),OP_LoadI | (1u<<8),OP_DivI | (1u<<24),OP_Halt};
    make_image(image,badcode,4); upload(image,67,false);
    assert(command(2,NULL,0,false)==REPLY_ACK);
    assert(command(3,NULL,0,false)==REPLY_ACK); EasyCon_script_task();
    assert(status().state==EASYCON_SCRIPT_ERROR && status().error_code==ECS_ERR_DIVZERO);
    assert(report.Button==0);
    const uint32_t filecode[]={OP_CallN | (255u << 24), ECS_SYSCALL_FLAG | ECS_SYSCALL_FOPEN, OP_Halt};
    make_image(image,filecode,3); image[10]=ECS_FEAT_FILE;
    upload(image,63,false);
    assert(command(2,NULL,0,false)==REPLY_ERROR);
    // A syscall-looking EXT data word must not be scanned as an instruction.
    const uint32_t printcode[]={OP_CallN | (2u << 16) | (255u << 24), ECS_SYSCALL_FLAG | ECS_SYSCALL_FWRITE, OP_Halt};
    make_image(image,printcode,3); image[10]=ECS_FEAT_FILE;
    upload(image,63,false);
    assert(command(2,NULL,0,false)==REPLY_ACK);
    assert(command(3,NULL,0,false)==REPLY_ACK); EasyCon_script_task();
    assert(status().state==EASYCON_SCRIPT_FINISHED);
    const uint32_t loop[]={OP_Jmp | 0xffffff00u};
    make_image(image,loop,1); upload(image,55,false);
    assert(command(2,NULL,0,false)==REPLY_ACK);
    assert(command(3,NULL,0,false)==REPLY_ACK);
    EasyCon_script_task(); // Budget must yield even without WAIT.
    assert(ecx_busy());
    assert(command(4,NULL,0,false)==REPLY_ACK);
    EasyCon_script_task();
    assert(!ecx_busy() && status().state==EASYCON_SCRIPT_STOPPED);
    const uint8_t oversized_legacy[]={CMD_READY,0,0,21,0,CMD_FLASH};
    for (size_t i=0;i<sizeof(oversized_legacy);i++) EasyCon_serial_task(oversized_legacy[i]);
    assert(reply==REPLY_ERROR);
    // Truncated frame times out, restoring the original command parser.
    EasyCon_serial_task(CMD_READY); EasyCon_serial_task(CMD_ECX); EasyCon_serial_task(0);
    now+=1100000; EasyCon_serial_task(CMD_READY); EasyCon_serial_task(CMD_VERSION);
    assert(reply==VERSION);
    assert(command(5,NULL,0,false)==REPLY_ACK && !ecx_present() && stored_size==0);
    puts("ECX upload/CRC/retry/persistence/autostart/stop/error/parser recovery passed");
}
