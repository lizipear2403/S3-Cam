/**
 * This example takes a picture every 5s and print its size on serial monitor.
 */

// =============================== SETUP ======================================

/**
 * 2. Kconfig setup
 *
 * If you have a Kconfig file, copy the content from
 *  https://github.com/espressif/esp32-camera/blob/master/Kconfig into it.
 * In case you haven't, copy and paste this Kconfig file inside the src
 * directory. This Kconfig file has definitions that allows more control over
 * the camera and how it will be initialized.
 */
#include "esp_err.h"
#include "sd_protocol_types.h"
#include "sdkconfig.h"
#include <stdbool.h>
#include <stdio.h>
#define MAX_CHAR_SIZE 16384
#define MOUNT_POINT "/sd"

#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "mirf.h"
#include "sdmmc_cmd.h"
#include <stdbool.h>

#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_camera.h"
#include "soc/gpio_num.h"

#define BOARD_WROVER_KIT 1

#include "camera_pinout.h"

static const char *TAG = "example:take_picture";

// #if ESP_CAMERA_SUPPORTED
static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20 * 1000 * 1000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_RGB565, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size =
        FRAMESIZE_QVGA, // QQVGA-UXGA, For ESP32, do not use sizes above QVGA
                        // when not JPEG. The performance of the ESP32-S series
                        // has improved a lot, but JPEG mode always gives better
                        // frame rates.

    .jpeg_quality = 12, // 0-63, for OV series camera sensors, lower number
                        // means higher quality
    .fb_count = 1, // When jpeg mode is used, if fb_count more than one, the
                   // driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
    .jpeg_buffer_size = 0, // Set to a custom size in bytes for JPEG mode, or 0
                           // to use the default size
};

sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

static void sdcard_init(sdmmc_slot_config_t *slot) {
  slot->clk = GPIO_NUM_39;
  slot->cmd = GPIO_NUM_38;
  slot->d0 = GPIO_NUM_40;
  slot->width = 1;
}

static const char *SDTAG = "SD Card IO";

static esp_err_t write_file(const char *path, char *data) {
  ESP_LOGI(SDTAG, "Opening file %s", path);
  FILE *f = fopen(path, "w");
  if (f == NULL) {
    ESP_LOGE(SDTAG, "Failed to open file for writing");
    return ESP_FAIL;
  }
  fprintf(f, data);
  fclose(f);
  ESP_LOGI(SDTAG, "File written");

  return ESP_OK;
}

static esp_err_t read_print_file(const char *path) {
  ESP_LOGI(SDTAG, "Reading file %s", path);
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ESP_LOGE(SDTAG, "Failed to open file for reading");
    return ESP_FAIL;
  }
  char line[64];
  printf("break\n");
  fgets(line, sizeof(line), f);
  fclose(f);

  // strip newline
  // char *pos = strchr(line, '\n');
  // if (pos) {
  //   *pos = '\0';
  // }
  ESP_LOGI(SDTAG, "Read from file: '%s'", line);

  return ESP_OK;
}

static esp_err_t init_camera(void) {
  // initialize the camera
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera Init Failed");
    return err;
  }

  return ESP_OK;
}

void app_main(void) {
  if (ESP_OK != init_camera()) {
    return;
  }
  sdcard_init(&slot_config);

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = 1,
      .max_files = 8,
      .allocation_unit_size = 16384,
      .disk_status_check_enable = 0,
      .use_one_fat = 0

  };
  sdmmc_card_t *sd;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.unaligned_multi_block_rw_max_chunk_size = 8;
  const char *mount_point = MOUNT_POINT;

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config,
                                          &mount_config, &sd);
  if (ret != ESP_OK) {
    ESP_LOGE(SDTAG, "Mount Failed");
  }
  ESP_LOGI(SDTAG, "Mount on %s", mount_point);
  sdmmc_card_print_info(stdout, sd);

  const char *file_test_path = "/sd/test";
  char buf[MAX_CHAR_SIZE];
  snprintf(buf, MAX_CHAR_SIZE, "Test Content\n");
  ret = write_file(file_test_path, buf);

  if (ret != ESP_OK) {
    ESP_LOGE(SDTAG, "Write Faild");
    return;
  }
  read_print_file(file_test_path);

  while (1) {
    ESP_LOGI(TAG, "Taking picture...");
    camera_fb_t *pic = esp_camera_fb_get();

    // use pic->buf to access the image
    ESP_LOGI(TAG, "Picture taken! Its size was: %zu bytes", pic->len);
    esp_camera_fb_return(pic);

    vTaskDelay(5000);
  }
}
