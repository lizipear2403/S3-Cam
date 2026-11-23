#pragma once

// DVP 摄像头（ESP32-S3-CAM，nulllaborg 兼容板）
#define CAM_PIN_PWDN  -1 // 未连接
#define CAM_PIN_RESET -1 // 软件复位
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF  7
#define CAM_PIN_PCLK  13
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD  4
#define CAM_PIN_SIOC  5
#define CAM_PIN_D0    11
#define CAM_PIN_D1    9
#define CAM_PIN_D2    8
#define CAM_PIN_D3    10
#define CAM_PIN_D4    12
#define CAM_PIN_D5    18
#define CAM_PIN_D6    17
#define CAM_PIN_D7    16

// SD 卡（1-bit SDIO）
#define SD_PIN_CLK 39
#define SD_PIN_CMD 38
#define SD_PIN_D0  40

// S3 <-> RP2040 链路：S3 为 SPI slave，RP2040 为 master 并提供时钟
// 沿用原 nRF24 排针位置；radio 已全部移交 RP2040，S3 侧不再有 nRF24。
#define LINK_PIN_MOSI  41 // 原 NRF_MOSI
#define LINK_PIN_MISO  46 // 原 NRF_MISO（S3 输出）
#define LINK_PIN_SCLK  42 // 原 NRF_SCK
#define LINK_PIN_CS    47 // 原 NRF_CSN
#define LINK_PIN_READY 14 // 原 NRF_CE：S3 就绪通知（原 NRF_IRQ 与 CE 同脚，已废弃）
