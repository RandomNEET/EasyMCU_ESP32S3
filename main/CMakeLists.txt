idf_component_register(
    SRCS
        "main.c"
        "ecx/ecx_runtime.c"
        "ecx/vendor/ecs_vm.c"
        "easycon/easycon.c"
        "hid/hid_device.c"
        "uart/uart_handler.c"
        "storage/storage.c"
        "display/oled_status.c"
    INCLUDE_DIRS
        "."
        "easycon"
        "hid"
        "uart"
        "storage"
        "display"
        "ecx"
    REQUIRES
        driver
        nvs_flash
        esp_timer
        tinyusb
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ECS_MAX_CALL_DEPTH=32)
