# ECX 下载协议

面向固件和工具维护者。操作步骤见 [脚本编译与下载](ecx-flash.md)。

兼容原有 EasyCon 控制协议，ECX 下载使用命令 0x90。每个请求先发 `A5 90`，再发送：

```
op:u8 | payload_len:u16 LE | payload | crc32:u32 LE
```

CRC 为标准 IEEE CRC32（与 Python zlib.crc32 一致），覆盖 op、length 和 payload。payload 最大 260 字节，帧内超过 1 秒无新字节时丢弃残帧，下一个字节恢复旧控制协议解析。所有长度包含 raw 数据，payload 中的高位字节不作为命令解释。

| op | payload | 用途 |
| --- | --- | --- |
| 0 | image_size:u32, image_crc:u32, auto_run:u8 | 开始新下载，申请暂存区 |
| 1 | offset:u32, data:1..256 bytes | 顺序分块；已接收且内容一致的重复块可确认 |
| 2 | 空 | 校验完整长度、CRC 和 VM 镜像结构，再保存 |
| 3 | 空 | 启动已保存的 ECX 程序 |
| 4 | 空 | 请求停止并释放按键 |
| 5 | 空 | 清除 ECX 程序及未完成下载 |

响应：FF=接受，FE=运行中／忙，00=拒绝。运行时不接受替换／清除镜像；需先停止。每次只允许一个程序、一个串口发送下载帧；不要同时使用 UART 和 CDC 下载。帧没有自动重传，遇到超时先等待 1 秒再完整重新上传。

暂存区独立于已安装程序；下载中断、CRC/格式校验失败或保存失败均不替换内存中的已安装程序。完整镜像以单个 NVS blob 保存于已有 `storage` 分区、`ecx` 命名空间、`program` 键；开机重新校验后加载。上传无需擦除固件或更改分区。blob 包含 `ECF2`、长度、CRC、自动运行标记和镜像。固件使用 NVS 的 blob 更新机制；断电恢复仍需实板验证。

ECX 镜像的实际魔数是上游 EcxWriter.Magic 的小端值 `0x32435845`（字节 `EXC2`），format_version=2；上游注释称 ECX2。本实现与上游数值保持一致。
