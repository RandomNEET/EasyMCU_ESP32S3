# 脚本状态测试

使用宿主机 C 编译器运行实际解释器代码，模拟存储、时钟及 HID 回调，覆盖 PC 控制报告计数、活动时间、心跳过滤、BUSY 报告过滤和串口命令启动、等待倒计时、停止后计时冻结、重新启动清零、自然结束。FreeRTOS shim 仅供此单线程测试，不验证多核竞争或屏幕硬件。

```sh
cc -std=c11 -Wall -Wextra -Werror -Wno-sign-compare \
  -I tests/stubs -I main/easycon -I main/hid \
  tests/test_script_status.c main/easycon/easycon.c \
  -o /tmp/easymcu-test-status
/tmp/easymcu-test-status
```

`-Wno-sign-compare` 对应解释器已有的 `0x80000000` 哨兵值比较。

固件编译：`devenv shell -- idf-board n16r8 build`。
屏幕需要按 `doc/sheets.md` 的步骤上板验收。

# OLED SPI 回归测试

模拟 ESP32-S3 非 DMA SPI 的 64 字节传输上限，检查 8 页各分两块发送、列偏移、帧数据、完成刷新后开屏，以及传输失败时的资源释放。还覆盖 PC ACTIVE/IDLE 在 2 秒阈值处的切换和 NO SCRIPT 的显示。该测试不替代实物亮屏验证。

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -I tests/oled_stubs -I main/display -I main/easycon -I main/hid \
  tests/test_oled_spi.c main/display/oled_status.c \
  -o /tmp/easymcu-test-oled
/tmp/easymcu-test-oled
```
