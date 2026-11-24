# Demo 目标与达成路径

> 目标：**现场跑出实时画面**。本文只写为达成 demo 必须做的事和判据，不重复设计推导。
> 状态：v0.1 · 2026-11-24
> 范围：摄像头端（S3 + RP2040）↔ 2.4 GHz 空口 ↔ 接收端（P4）
> 已确定的常量（地址、帧格式、掩码、引脚等）见**附录**。

## 1. Demo 的定义

题目基本要求就是 demo 门槛：

| 条目 | 指标 |
|---|---|
| 1.3 | 无遮挡视距 ≥ 10 m，接收端显示实时画面 |
| 1.4 | 连续 60 s，画面连续显示率 ≥ 90%（黑屏/冻结/花屏计无效） |
| 1.6 | 端到端延迟 ≤ 500 ms |
| 1.7 | 分辨率 ≥ 320×240，平均帧率 ≥ 10 fps |

两级判据：

- **能演示** = 屏上出来第一帧实时画面
- **能计分** = 上表四项实测达标

## 2. 当前状态

| 层面 | 状态 |
|---|---|
| 架构、引脚账、主从方向、SPI 协议、帧格式、地址 | 已确定 |
| S3 端代码（拍照 / SD 旁路 / SPI slave） | 已实现，编译通过，commit `7a71e15`，**未上板** |
| RP2040 代码 | 未写（约束已定，见 `Cam-S3/RP2040/约束.md`） |
| P4 拼帧 / 屏驱 | 草稿有 bug，屏型号待定（见 `Recv-S3/P4-Recv/约束.md`） |
| 端到端联调 | 无 |
| **可演示度** | **0（还没有画面）** |

核心问题：没有任何一段做过实机联调，所有带宽/延迟都是估算。

## 3. 最小闭环（信号流 5 段）

| # | 段 | 负责端 | 现状 | 缺口 |
|---|---|---|---|---|
| 1 | DVP → JPEG → SD 旁路 | S3 | 已实现（编译） | 上板验证 |
| 2 | S3 → RP2040（SPI） | 两端 | S3 slave 已实现 | **RP2040 master 未写** |
| 3 | RP2040 → nRF24 → 空口 | RP2040 | 未写 | 单路驱动 + 分片发送 |
| 4 | 空口 → 收包 → 重组 | P4 | 草稿，拼帧有 bug | 修 `× 28`、位图、超时丢弃 |
| 5 | JPEG → MIPI DSI 屏 | P4 | 未实现 | 屏型号 / 驱动 IC 待定 |

**第一处断点在第 2 段**：S3 作为 slave 已就绪，但没有 master 发起事务，数据出不去。

## 4. 达成步骤（按依赖顺序）

| 步 | 做什么 | 完成判据 |
|---|---|---|
| S0 | 装 RP2040 工具链（arm-none-eabi + pico-sdk 2.x），先跑通 `pico-examples/blink` | 产出 `.uf2`，板子能跑 |
| S1 | 按约束写 RP2040：SPI master 取帧 + **单路** nRF24 分片发送 | 编译通过；USB CDC 打印帧号/长度/包数 |
| S2 | 单独验证 S3 ↔ RP2040 取帧（先不接空口） | RP2040 读出的长度头与 S3 帧长一致，JPEG 头 `FF D8` 正确 |
| S3 | 空口打通：单路 nRF24 对发 | 接收端收到完整 FramePkt 序列 |
| S4 | 修 P4 拼帧 + 点亮 MIPI 屏 + JPEG 解码 | 屏上能显示一张完整 JPEG |
| S5 | 两端联调 | **出第一帧实时画面 = demo 达成** |
| S6 | 按 §1 测四项指标 | 10 m / 60 s / 帧率 / 延迟 均有记录 |
| S7 | 之后才叠：OSD、FHSS、3 路并行、电池组、录制回放 | 不在 demo 阶段 |

S2、S3 分开做，是为了把「SPI 时序」和「无线链路」两类问题隔离。

## 5. 会挡住 demo 的问题（必须处理）

| 问题 | 处理 |
|---|---|
| 帧长上限：`sub_frame_id` 8 bit → 单帧 ≤ 255 包 = 7140 B | 实测 QVGA q24 帧长；超限时降 `jpeg_quality` 或显式截断并打日志，不得静默回绕 |
| RF_CH 90 = 2490 MHz，超出 ISM 2400–2483.5 MHz | 联调可用；正式提交前统一改到 0–83 |
| 1-byte CRC / 地址镜像 | 两端严格一致，否则 ACK 不通（见附录 A.1、A.2） |
| `P4-Recv.c` 拼帧索引缺 `× 28` | 改为 `sub_frame_id * 28` |
| `pic_buf` 32768 B 与注释 16384 不符 | 按 7140 B 上限统一 |
| S3 SPI 时序未上板验证 | S2 先单独验证；READY 用电平检测，不用边沿 |
| 3 个 radio 同机并发互扰 | demo 阶段单路，不涉及；S7 再实测 |

## 6. 明确不做（本阶段）

FHSS / ≥16 频点、3 路并行、VGA 640×480、OSD 全字段、录制回放与断电保护、双端遥测、电池组与充电保护、射频检测接口。

## 7. 回退方案

| 卡点 | 回退 |
|---|---|
| QVGA q24 帧长超 7140 B | 降 quality 到 30–40，或先只发前 255 包看上半幅画面 |
| SPI 取帧不通 | 降速到 1 MHz、缩短连线、先只传固定小帧 |
| 屏驱动不通 | 先用 USB CDC 打印 JPEG 长度与帧号验证链路，屏后接 |
| 空口不通 | 两块 nRF24 直连对发固定包，排除地址/CRC/速率不一致 |

---

# 附录 A：已确定的常量

## A.1 地址（RADDR / TADDR）

5 字节 ASCII，**保持不变**；两端为镜像关系。

| 端 | RADDR | TADDR | 来源 |
|---|---|---|---|
| 摄像头端（原 S3，现由 RP2040 承担） | `CAMRA` | `CTROL` | 原 `S3-Cam.c` 草稿 |
| 接收端（P4） | `CTROL` | `CAMRA` | `P4-Recv.c:109-110` |

寄存器语义（mirf 库）：`setRADDR` 写 `RX_ADDR_P1`；`setTADDR` 写 `RX_ADDR_P0` + `TX_ADDR`。
RP2040 自写驱动时须对齐：`TX_ADDR` 与 `RX_ADDR_P0` = TADDR，`RX_ADDR_P1` = RADDR。

## A.2 无线参数

| 项 | 值 |
|---|---|
| 模块 | nRF24L01+ / E01-ML01IPX |
| 空中速率 / 功率 | 2 Mbps / 0 dBm |
| CRC | **1 byte**（mirf `mirf_CONFIG`，`CRCO=0`） |
| 地址宽度 | 5 B（`SETUP_AW = 0x03`） |
| payload | 32 B，pipe0 自动 ACK |
| SETUP_RETR | `0x5F` |
| RF_CH | `90`（联调临时值，正式须 0–83） |
| SPI | mode 0，≤ 10 MHz（mirf 用 4 MHz） |

## A.3 S3 ↔ RP2040 SPI 契约

| 项 | 值 |
|---|---|
| 主从 | RP2040 = master（供时钟），S3 = slave |
| SPI | mode 0，MSB first |
| 事务长度 | 固定 32768 B（`32 * 1024`），一次 CS 拉低→拉高 |
| 帧布局 | `[0..3]` 小端 uint32 = JPEG 长度；`[4..4+len)` JPEG；尾部无意义，按长度头截取 |
| READY | S3 输出；高 = 一帧就绪；事务完成后由 S3 拉低 |
| 握手 | **电平检测**：等 READY 高 → 发起事务 → 等 READY 低；禁止边沿中断 |
| 帧率 | 由 master 发起事务的节奏决定；S3 侧 `spi_slave_transmit(..., portMAX_DELAY)` 阻塞等 master |

## A.4 帧格式 `FramePkt`

```c
struct FramePkt {
    uint32_t pkt_info;      // 4 B，小端
    uint8_t  pkt_data[28];  // 28 B → 合计 32 B（nRF24 最大 payload）
};
```

| 位域 | 含义 |
|---|---|
| `[15:0]` | frame_id |
| `[23:16]` | sub_frame_id |
| `[31:24]` | expect_pkt_num（本帧总包数） |

写入位置 = `sub_frame_id * 28`。单帧最多 255 包 = 7140 B。

## A.5 掩码（`Mask.h`，两端一致）

```c
#define FRAME_ID_MASK     ((uint32_t)(0x0000FFFF))
#define FRAME_ID_OFFSET   0
#define SUB_FRAME_MASK    ((uint32_t)(0x00FF0000))
#define SUB_FRAME_OFFSET  16
#define EXPECT_PKT_NUM    ((uint32_t)(0xFF000000))
#define EXPECT_PKT_OFFSET 24
```

路径：`Cam-S3/S3-Cam/main/Mask.h`、`Recv-S3/P4-Recv/main/Mask.h`（内容一致）。

## A.6 引脚

**S3 侧**（`Cam-S3/S3-Cam/main/Pinout.h`，已实现）

| 用途 | 引脚 |
|---|---|
| DVP | 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18 |
| SD（1-bit SDIO） | CLK 39 / CMD 38 / DAT0 40 |
| S3 ↔ RP2040 | MOSI 41 / MISO 46 / SCLK 42 / CS 47 / READY 14 |

S3 可用 GPIO 仅约 6 个（DVP 14 + SDIO 3 + UART0 2 占用后），故 RP2040 必要。

**RP2040 侧**（建议值，见 `约束.md`，改动只改宏）

| 用途 | 建议值 |
|---|---|
| S3 链路 | `spi0`：SCK 2 / MOSI 3 / MISO 4 / CS 5 / READY 6，20 MHz |
| nRF24 | `spi1`：SCK 10 / MOSI 11 / MISO 12 / CSN 13 / CE 14 / IRQ 15 |

**P4 侧**：`P4-Recv.c` 现状 SCK 49 / MOSI 50 / MISO 48 / CE 23 / CSN 51 / IRQ 16；`Recv-S3/S3-Recv/` 与 `Recv-S3/PINOUT` 的 4/5/6/7/15/16 那套**已废弃，不得复用**。

## A.7 相机与 SD（S3 已固定）

| 项 | 值 |
|---|---|
| 分辨率 / 格式 | QVGA 320×240 / `PIXFORMAT_JPEG` |
| 质量 | `jpeg_quality = 24`（值越小质量越高） |
| XCLK | 20 MHz |
| 帧缓冲 | `fb_count = 1`，`CAMERA_FB_IN_PSRAM` |
| SD | 1-bit SDIO，挂载 `/sd`，每帧独立文件并 `fclose` 落盘 |

## A.8 代码 / 文档索引

| 内容 | 位置 |
|---|---|
| 设计草案 | `AI-docs/设计草案.md` |
| RP2040 约束 | `Cam-S3/RP2040/约束.md` |
| P4 约束 | `Recv-S3/P4-Recv/约束.md` |
| S3 实现 | `Cam-S3/S3-Cam/main/S3-Cam.c`（commit `7a71e15`） |
| 本仓库 demo 文档 | `Cam-S3/S3-Cam/Demo目标.md` |

---

# 附录 B：工具链

| 端 | 工具链 |
|---|---|
| S3 | `getidf` = ESP-IDF v6.0.1 + py3.12 venv |
| P4 | **必须 ESP-IDF v6.1.1**（`/home/lizi/esp/v6.1.1/esp-idf`）；不用 `getidf`；忽略其他版本编译错误 |
| RP2040 | `arm-none-eabi-gcc` + `newlib` + CMake/Ninja + pico-sdk 2.x（CachyOS 用 pacman 安装） |

# 附录 C：资料

- `题目.pdf` — 赛题与评分
- `E01-ML01IPX_UserManual_CN_v1.5.pdf` — 无线模块手册
- `info.md` / `infoRepos/esp32s3-cam/` — S3-CAM 板卡引脚与原理图
- `Cam-S3/mirf/`、`thirdDrivers/esp-idf-mirf/` — nRF24 驱动参考
