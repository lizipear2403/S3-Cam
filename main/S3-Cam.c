/*
 * 摄像头端（ESP32-S3-CAM）
 *
 * 角色：SPI slave，RP2040 为 master 并提供时钟。
 * 流程：拍照 -> 写 SD（旁路录制）-> 把同一帧经 SPI 交给 RP2040。
 *       JPEG 帧缓冲直接发送，不经 SD 回读，保证实时性。
 *
 * 链路协议（与 RP2040 约定）：
 *   一次 CS 事务固定 LINK_BUF_SIZE 字节：
 *     [0..3]  小端 uint32，本帧 JPEG 长度
 *     [4..]   JPEG 数据；尾部字节无意义，按长度头截取
 *   READY：S3 准备好一帧后置高，事务完成后置低。
 *   RP2040 见 READY 上升沿即发起事务取帧；帧率由 master 决定。
 *   RP2040 上电时若 READY 已为高，应直接发起一次事务（避免漏掉首个上升沿）。
 *
 * 注意：spi_slave_transmit 会阻塞等待 master 取走数据，未接 RP2040 时停在此处。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_slave.h"
#include "esp_camera.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "Pinout.h"

static const char *TAG = "S3-Cam";

#define MOUNT_POINT   "/sd"
#define LINK_HOST     SPI2_HOST
#define LINK_BUF_SIZE (32 * 1024) // 4B 长度头 + JPEG；须大于最大帧长

static const camera_config_t cam_cfg = {
    .pin_pwdn     = CAM_PIN_PWDN,
    .pin_reset    = CAM_PIN_RESET,
    .pin_xclk     = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d0 = CAM_PIN_D0,
    .pin_d1 = CAM_PIN_D1,
    .pin_d2 = CAM_PIN_D2,
    .pin_d3 = CAM_PIN_D3,
    .pin_d4 = CAM_PIN_D4,
    .pin_d5 = CAM_PIN_D5,
    .pin_d6 = CAM_PIN_D6,
    .pin_d7 = CAM_PIN_D7,

    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href  = CAM_PIN_HREF,
    .pin_pclk  = CAM_PIN_PCLK,

    .xclk_freq_hz = 20 * 1000 * 1000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_QVGA, // 320x240
    .jpeg_quality = 24,
    .fb_count     = 1,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
};

static uint8_t *tx_buf;  // DMA 缓冲：[4B 长度][JPEG]
static unsigned frame_no;
static bool     sd_ready;

/* ---------------- SD：旁路录制 ---------------- */

static void sd_init(void) {
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk   = SD_PIN_CLK;
    slot.cmd   = SD_PIN_CMD;
    slot.d0    = SD_PIN_D0;
    slot.width = 1;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = 1,
        .max_files              = 4,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_card_t *card = NULL;
    sdmmc_host_t  host = SDMMC_HOST_DEFAULT();
    sd_ready = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mount, &card) == ESP_OK;
    if (!sd_ready) {
        ESP_LOGW(TAG, "SD 挂载失败，仅实时链路可用");
    }
}

// 每帧独立成文件并立即 fclose 落盘：断电最多丢当前帧（扩展 2.6）
static void store_frame(const uint8_t *data, size_t len) {
    if (!sd_ready) {
        return;
    }

    char path[32];
    snprintf(path, sizeof(path), MOUNT_POINT "/f%05u.jpg", frame_no);

    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGW(TAG, "SD 写入不完整：%u/%u", (unsigned)written, (unsigned)len);
    }
}

/* ---------------- SPI slave 链路 ---------------- */

static void on_tx_ready(spi_slave_transaction_t *t) {
    (void)t;
    gpio_set_level(LINK_PIN_READY, 1);
}

static void on_tx_done(spi_slave_transaction_t *t) {
    (void)t;
    gpio_set_level(LINK_PIN_READY, 0);
}

static void link_init(void) {
    gpio_config_t ready = {
        .mode         = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LINK_PIN_READY,
    };
    ESP_ERROR_CHECK(gpio_config(&ready));
    gpio_set_level(LINK_PIN_READY, 0);

    // 无 master 时上拉，避免杂散时钟触发误传输
    gpio_set_pull_mode(LINK_PIN_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(LINK_PIN_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(LINK_PIN_CS,   GPIO_PULLUP_ONLY);

    spi_bus_config_t bus = {
        .mosi_io_num     = LINK_PIN_MOSI,
        .miso_io_num     = LINK_PIN_MISO,
        .sclk_io_num     = LINK_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LINK_BUF_SIZE,
    };
    spi_slave_interface_config_t slave = {
        .spics_io_num  = LINK_PIN_CS,
        .mode          = 0,
        .queue_size    = 1,
        .post_setup_cb = on_tx_ready,
        .post_trans_cb = on_tx_done,
    };
    ESP_ERROR_CHECK(spi_slave_initialize(LINK_HOST, &bus, &slave, SPI_DMA_CH_AUTO));

    tx_buf = spi_bus_dma_memory_alloc(LINK_HOST, LINK_BUF_SIZE, 0);
    assert(tx_buf);
    memset(tx_buf, 0, LINK_BUF_SIZE);
}

static void send_frame(const uint8_t *data, size_t len) {
    if (len + 4 > LINK_BUF_SIZE) {
        ESP_LOGW(TAG, "帧过大(%u)，跳过", (unsigned)len);
        return;
    }

    uint32_t n = len;
    tx_buf[0] = n;
    tx_buf[1] = n >> 8;
    tx_buf[2] = n >> 16;
    tx_buf[3] = n >> 24;
    memcpy(tx_buf + 4, data, len);

    spi_slave_transaction_t t = {
        .length    = LINK_BUF_SIZE * 8, // 定长事务，长度单位是 bit
        .tx_buffer = tx_buf,
    };
    esp_err_t err = spi_slave_transmit(LINK_HOST, &t, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI 传输失败：%s", esp_err_to_name(err));
    }
}

/* ---------------- main ---------------- */

void app_main(void) {
    ESP_ERROR_CHECK(esp_camera_init(&cam_cfg));
    sd_init();   // SD 是旁路，失败不影响实时链路
    link_init(); // 必须在相机就绪后，避免 READY 提前拉高

    ESP_LOGI(TAG, "就绪：QVGA JPEG，等待 RP2040(master) 取帧");

    while (1) {
        camera_fb_t *pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGW(TAG, "采集失败");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        store_frame(pic->buf, pic->len);
        send_frame(pic->buf, pic->len);
        esp_camera_fb_return(pic);

        frame_no++;
    }
}
