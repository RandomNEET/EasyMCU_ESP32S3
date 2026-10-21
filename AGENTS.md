# 项目协作指南

## 范围与约定

- 这是 ESP32-S3 固件仓库，使用 ESP-IDF、FreeRTOS 和 TinyUSB。默认验证板型为 N16R8，同时提供 N8R8、N8R2 配置。
- 使用中文与用户沟通，说明实际改动、验证结果和未验证的硬件行为。
- EasyCon 是独立的上位机仓库。可读取其源码核对协议，但不要修改上位机代码，除非用户明确要求。
- 文档、示例和工具不写个人机器绝对路径；使用相对路径、参数或环境变量。串口用 `SERIAL_PORT` 占位。
- 不覆盖用户已有改动，不顺带调整无关功能。仅在用户要求时提交或推送。

## 开发环境与命令

优先使用仓库的 devenv 环境，依赖由 `devenv.nix`、`devenv.yaml` 和 `devenv.lock` 管理。需要新增开发工具时加入环境，不依赖个人全局安装。

```sh
devenv shell
idf-board n16r8 build
sh tests/run-host.sh
fmt main/display/oled_ui.c
```

- 环境外可运行 `devenv shell -- idf-board n16r8 build`。
- `idf-board` 支持 `n16r8`、`n8r8`、`n8r2`，分别使用 `build/<板型>` 内的配置和产物。不要混用板型构建目录或手改生成的 sdkconfig。
- 固件烧录：`idf-board n16r8 -p SERIAL_PORT flash`。实际串口和板型以用户设备为准。
- `devenv test` 检查环境工具是否可用，不替代主机测试或固件构建。
- `fmt` 使用 treefmt 调用 nixfmt、clang-format、ruff 和 shfmt；规则见 `treefmt.toml`、`.clang-format`。优先只格式化修改文件，避免无关差异。
- `fmt --ci` 检测到格式变化时失败，但仍会修改文件，并非只读检查。
- 不格式化或直接修改 `main/ecx/vendor/`、`managed_components/`、构建和缓存目录，除非任务明确涉及它们。

## 代码位置

| 路径                         | 职责                                                             |
| ---------------------------- | ---------------------------------------------------------------- |
| `main/main.c`                | 初始化和 FreeRTOS 任务调度                                       |
| `main/easycon/`              | 原有协议、脚本解释器与状态快照                                   |
| `main/ecx/ecx_runtime.c`     | ECX 上传、校验、持久化和板载执行                                 |
| `main/ecx/vendor/`           | 第三方 ECX VM 源码，保留来源和许可证                             |
| `main/hid/`                  | USB HID、报告状态和显示用输入快照                                |
| `main/uart/`                 | 串口通信                                                         |
| `main/storage/`              | 存储接口                                                         |
| `main/display/oled_status.c` | SH1106 初始化、SPI 刷新、显示任务和动画调度                      |
| `main/display/oled_ui.c`     | 纯帧缓冲绘图，入口为 `oled_render_dashboard`、`oled_render_face` |
| `tools/ecx_compile.py`       | 调用现有 EasyCon CLI 编译脚本                                    |
| `tools/ecx_flash.py`         | 应用协议握手、ECX 上传及执行控制                                 |
| `tests/`                     | 主机测试及硬件接口替身                                           |

## 并发与显示约束

- 当前 core 0 执行 tick（优先级 6）、UART RX 和 CDC RX（优先级 5）；core 1 执行 HID TX（优先级 4）和脚本（优先级 3）。OLED 任务优先级 1，不绑定核心，每帧刷新后等待 100 ms。
- OLED 通过快照获取脚本和 HID 状态；临界区内不要绘图、传输 SPI、写存储或调用可能阻塞的接口。
- 动画由显示任务维护，使用时间戳调度，不额外阻塞脚本或 HID。绘图层保持无硬件 I/O、无共享状态读取，便于主机测试。
- 控制器短按显示保留约 200 ms；真实 HID 松开报告必须立即生效，不能跟随视觉高亮延迟。
- 接线以 `doc/sheets.md` 为准：SCLK GPIO12、MOSI GPIO11、RES GPIO46、DC GPIO9、CS GPIO10。GPIO10 不得复用为调试输出。
- SH1106 使用 128×64 帧缓冲、8 页、列偏移 2；SPI2 mode 0、1 MHz。禁用 DMA 时单次传输最多 64 字节，一页分两次发送，不能通过增大 `max_transfer_sz` 绕过 FIFO 上限。
- ONLINE 表示电脑控制来源，OFFLINE 表示板载脚本会话，STANDBY 表示尚无控制会话；它们不是网络连接状态。板载脚本运行或等待时优先显示板载状态。
- PC ACTIVE/IDLE 依据最近有效控制报告的 2 秒超时判断，不代表上位机脚本实际开始或结束。不要把心跳当作控制活动。

## ECX 工具与协议

- Python 工具保持 Windows、Linux、macOS 通用；不要在工具内隐式调用 Nix 安装 SDK。
- .NET 10 SDK 由 devenv 提供；上位机仓库位置通过现有参数或 `EASYCON_ROOT` 指定。
- ECX 上传连接运行中的应用固件，不使用 ESP ROM BOOT 下载模式；与固件烧录区分。
- 修改协议、VM 或存储流程时，保留 CRC 校验、重传、残帧恢复、能力检查及停止后的按键释放行为。
- 详细用法和协议见 `doc/ecx-flash.md`、`doc/ecx-protocol.md`。

## 验证与提交

- 按改动范围运行检查。显示、HID、解释器或协议变更后运行 `sh tests/run-host.sh`；固件变更同时构建 `idf-board n16r8 build`。
- 主机测试不验证真实多核竞争、USB 时序、屏幕电气行为或 Flash 断电恢复。没有实板验证时明确说明，不把构建通过等同于硬件正常。
- 更新行为时同步相关文档和有意义的回归测试；提交前运行 `git diff --check`。
