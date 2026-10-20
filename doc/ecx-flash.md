# ECX 脚本编译与下载

固件支持将 EasyCon CLI 生成的 `.ecx` 保存到板子并独立执行。EasyCon GUI 的“编译烧录”按钮尚未适配，请使用本仓库的命令行工具。PC 在线控制和 OLED 状态显示仍可使用。

本文命令均在固件仓库根目录运行。`SERIAL_PORT` 为占位符，请替换为实际串口：Linux 通常为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`，Windows 为 `COM3` 等，macOS 为 `/dev/cu.*` 设备。

## 准备环境和固件

使用 devenv：

```sh
devenv shell
```

环境包含 ESP-IDF、交叉编译器、Python、pyserial 和 .NET 10 SDK，版本由项目配置和锁文件管理。首次进入环境可能下载依赖。

非 devenv 环境也可运行两个 Python 工具。Windows、Linux、macOS 均需安装 Python 3；编译脚本还需 .NET 10 SDK，下载脚本需 pyserial：

```sh
dotnet --list-sdks
python -m pip install pyserial
```

确认 SDK 列表包含 10.x。固件本身仍需 ESP-IDF 环境构建。

首次使用 ECX 功能前，先烧录支持该功能的固件。以 N16R8 板型为例：

```sh
idf-board n16r8 -p SERIAL_PORT -b 115200 flash
```

烧录固件时按住 BOOT、按一下 RST 进入 ROM 下载模式；烧录完成后松开 BOOT，再按 RST 进入应用。OLED 接线及 GPIO46 复位线的注意事项见 [屏幕说明](sheets.md)。

**后续下载脚本时不要进入 BOOT 模式**，板子应正在运行应用固件。ROM 下载接口不能接收 ECX 脚本协议。

## 编译脚本

默认目录结构：

```text
workspace/
├── EasyCon/
└── EasyMCU_ESP32S3/
```

使用现有 EasyCon CLI 编译仓库提供的 [示例脚本](../examples/pad-test.ecs)：

```sh
python tools/ecx_compile.py examples/pad-test.ecs -o build/scripts/pad-test.ecx
```

省略 `-o` 时，在源文件旁生成同名 `.ecx`。编译脚本仅调用 PATH 中的 `dotnet`，不会调用 Nix 或安装 SDK。首次编译可能下载 NuGet 包，也会生成 EasyCon 正常的 `bin/obj` 构建缓存。

EasyCon 不在默认位置时，用 `--easycon` 指定仓库根目录，或设置环境变量 `EASYCON_ROOT`：

```sh
python tools/ecx_compile.py examples/pad-test.ecs --easycon ../EasyCon -o build/scripts/pad-test.ecx
```

Windows PowerShell 的相对路径写法同样适用：

```powershell
python tools/ecx_compile.py .\examples\pad-test.ecs --easycon ..\EasyCon -o .\build\scripts\pad-test.ecx
```

路径包含空格时加引号。上位机源码无需修改。

## 下载、运行与停止

先关闭 EasyCon 的串口连接和串口监视器，确保只有一个程序占用设备。使用 COM 端口对应的 UART0，或应用提供的 USB CDC 端口。

```sh
python tools/ecx_flash.py -p SERIAL_PORT probe
python tools/ecx_flash.py -p SERIAL_PORT upload build/scripts/pad-test.ecx --start
python tools/ecx_flash.py -p SERIAL_PORT stop
python tools/ecx_flash.py -p SERIAL_PORT start
```

`probe` 只检查应用握手和固件版本，不下载程序。`upload` 默认只保存脚本；`--start` 表示保存后立即运行。工具的 OK 仅表示固件接受了请求，执行结果看 OLED 或 UART1 日志。

让脚本在以后上电时自动运行：

```sh
python tools/ecx_flash.py -p SERIAL_PORT upload build/scripts/pad-test.ecx --auto-run --start
```

清除保存的 ECX 程序：

```sh
python tools/ecx_flash.py -p SERIAL_PORT stop
# 等待 OLED 显示 STOPPED，再清除。
python tools/ecx_flash.py -p SERIAL_PORT clear
```

ECX 保存成功后，EasyCon 原有“远程运行／远程停止”也会启动／停止它。存在 ECX 时优先执行 ECX，清除后恢复旧脚本执行路径。电脑端“运行脚本”仍是在线发送控制报告，区别见 [屏幕状态说明](sheets.md)。

## 能力和限制

- 支持 VM2 算术、控制流、函数、字符串、数组、结构体，以及 WAIT、按键、摇杆和 RAND。
- 停止、结束、出错时释放按键并回中摇杆。停止为异步请求：延时中约一个 tick 响应，无延时循环按指令预算让出执行。
- PRINT 静默处理，不向控制串口输出文本；TIME 返回本次运行毫秒数；ALERT、BEEP、AMIIBO 为无操作。
- 不支持图像识别、屏幕采集、PC 文件系统和 FFI。编译器会给 PRINT 标记 FILE，因此固件允许该标记，再逐函数检查 syscall，拒绝 FOPEN、FREAD 等未支持操作。
- 镜像上限 64 KiB，调用深度上限 32；使用内部堆，不依赖 PSRAM，动态数据仍可能耗尽内存。
- 仅接受 ECX 格式版本 2，不接受 `.ecs` 文本、旧字节码或 `.hex/.bin` 固件文件。

ECX 的 OLED 顶部显示 ECX，大字显示 RUNNING、WAITING、STOPPED、FINISHED 或 ERROR；普通状态显示运行和等待时间，错误时显示英文原因及错误码，例如 `DIV BY ZERO 8`。结果码定义：0 正常、2 取消、8 除零、9 调用过深、10 未实现原生调用、12 内存不足。完整定义见 [ecs_vm.h](../main/ecx/vendor/ecs_vm.h)，布局见 [屏幕说明](sheets.md)。

## 串口排错

权限错误先检查设备权限。无回复时，关闭其他串口程序，松开 BOOT、按 RST，确认 OLED 已显示应用界面，再执行 `probe`。

工具默认保持 RTS 关闭；对 ttyUSB 关闭 DTR，对原生 USB CDC（ttyACM）启用 DTR。串口别名未被识别，或打开端口时复位较慢，可显式指定：

```sh
python tools/ecx_flash.py -p SERIAL_PORT --dtr on --boot-wait 3 probe
```

- HELLO 无回复：应用尚不可达，检查启动模式、端口、115200 波特率和占用情况。
- 握手成功但 BEGIN 被拒绝：确认板上已刷入支持 ECX 的固件；0x46 版本号本身不能证明 ECX 支持。
- DATA／COMMIT 被拒绝：检查具体错误阶段和 UART1 日志，可能是 CRC、镜像能力或存储校验失败。
- 返回 BUSY：先停止正在运行的脚本，等停止完成后重试。

协议和持久化布局见 [ECX 下载协议](ecx-protocol.md)。自动化测试及实板验收范围见 [测试说明](../tests/README.md)。
