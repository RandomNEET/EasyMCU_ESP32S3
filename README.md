# EasyMCU_ESP32S3

## 项目概述

EasyMCU_ESP32S3是一个基于ESP32-S3开发板的游戏手柄模拟项目，通过串口接收命令来模拟游戏手柄的按键动作。

## 支持的硬件版本

- **已测试版本**：ESP32-S3-DevKitC-1 N16R8
- **其他版本**：n8r2, n8r8（需要自行编译，不保证能用）

## 硬件连接

![ESP32-S3-DevKitC-1开发板](img/ESP32-S3-DevKitC-1.png)

1. **电脑连接**：
   - 板子背面标有"com"的USB-C口连接到电脑
   - 用于烧录固件和接收EasyCon命令

2. **Switch连接**：
   - 板子背面标有"usb"的USB-C口连接到Nintendo Switch
   - 用于模拟游戏手柄

## 开发环境（devenv）

安装 Nix（启用 flakes）和 devenv 后，在项目根目录运行：

```sh
devenv shell
idf-board n16r8 build
```

环境通过 `devenv.lock` 固定 ESP-IDF Nix 包及工具链版本，包含 Python 组件管理器、CMake、Ninja、ESP32-S3 交叉编译器和 .NET 10 SDK。当前锁定 ESP-IDF 5.5.2。首次进入环境和下载托管组件需要联网。

三种板型分别使用 `sdkconfig.defaults` 和对应的 `sdkconfig.n16r8`、`sdkconfig.n8r8` 或 `sdkconfig.n8r2`，生成配置及构建产物保存在 `build/<板型>/`：

```sh
idf-board n16r8 build
idf-board n8r8 build
idf-board n8r2 build
idf-board n16r8 menuconfig
idf-board n16r8 -p /dev/ttyUSB0 flash monitor
```

将串口路径替换为实际设备；退出串口监视器按 `Ctrl+]`。Linux 用户需要拥有串口设备的读写权限。

也可以从环境外直接运行 `devenv shell -- idf-board n16r8 build`，使用 `devenv test` 检查工具是否可用。如已安装 direnv，运行 `direnv allow` 后可通过 `.envrc` 自动进入环境。

SPI OLED 接线及显示字段见 [doc/sheets.md](doc/sheets.md)。已支持 SH1106 128×64 四线 SPI 屏幕，显示 PC 控制活动、按键／摇杆和 USB 连接状态；运行板载脚本时显示脚本状态、时间、字节码地址和等待时间。宿主机状态测试见 [tests/README.md](tests/README.md)。

## 板载 ECX 脚本

本分支支持将 EasyCon CLI 编译的 `.ecx` 下载到板子运行，无需修改上位机代码。下载、自动运行、停止和清除步骤见 [doc/ecx-flash.md](doc/ecx-flash.md)。GUI 的“编译烧录”按钮仍未适配。

## 烧录指南

### 固件路径

项目根目录下的 `/release_bins/` 文件夹

### 固件文件列表

- `EasyMCU_ESP32S3_n16r8.bin` - 适用于 ESP32-S3-DevKitC-1 N16R8（已测试）
- `EasyMCU_ESP32S3_n8r2.bin` - 适用于 ESP32-S3-DevKitC-1 N8R2 版本（需自行测试）
- `EasyMCU_ESP32S3_n8r8.bin` - 适用于 ESP32-S3-DevKitC-1 N8R8 版本（需自行测试）

### 进入烧录模式
1. 按住板子上的"BOOT"按钮
2. 按下并松开"RST"按钮
3. 松开"BOOT"按钮
4. 此时板子进入烧录模式

### 烧录软件推荐
![flash_download_tool烧录示例](img/flash_download_tool烧录示例.png)
- **ESP Flash Download Tool**：[官方下载链接](https://www.espressif.com/en/support/download/other-tools)
  - 选择"ESP32-S3"作为芯片类型
  - 选择固件文件
  - 地址设置为"0x0"
  - 选择COM
  - 点击"Start"开始烧录

## 使用方法

1. **安装EasyCon软件**：从 [EasyCon 官方 GitHub 仓库](https://github.com/EasyConNS/EasyCon) 下载并安装软件
2. **连接设备**：
   - 通过标有"com"的USB-C口将ESP32连接到电脑
   - 通过标有"usb"的USB-C口将ESP32连接到Nintendo Switch
3. **配置EasyCon**：
   - 在EasyCon软件中选择ESP32的COM端口
   - 配置按键映射和其他设置
4. **开始使用**：
   - 通过EasyCon软件发送命令
   - ESP32会模拟游戏手柄的按键动作

## 调试日志

如果需要查看调试日志：
1. 将CH341A模块连接到ESP32的UART1：
   - CH341A的TXD接ESP32的GPIO18（RX1）
   - CH341A的RXD接ESP32的GPIO17（TX1）
   - CH341A的GND接ESP32的GND
2. 使用串口助手打开CH341A的COM端口，波特率设置为115200，8N1
3. 观察ESP32的启动日志和运行状态

## 注意事项

- 只测试了ESP32-S3-DevKitC-1 N16R8版本
- 其他版本需要自行编译，不保证能用
- 烧录时请确保正确进入烧录模式
- 使用时请确保硬件连接正确，避免短路

## 故障排查

- **无法进入烧录模式**：请检查按键操作顺序是否正确
- **烧录失败**：请检查USB连接是否稳定，尝试更换USB线缆
- **游戏手柄未被识别**：请检查USB连接，确保ESP32正确连接到Nintendo Switch
- **EasyCon命令无响应**：请检查COM端口设置，确保波特率为115200
- **调试日志无输出**：请检查CH341A与ESP32的UART1连接

## 免责声明

本项目仅供学习和研究使用，使用本项目产生的任何后果由使用者自行承担。