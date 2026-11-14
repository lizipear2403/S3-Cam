idf_component_register(SRCS take_picture.c
                        PRIV_INCLUDE_DIRS .
                        PRIV_REQUIRES nvs_flash esp_psram esp_driver_sdmmc sdmmc fatfs mirf esp_driver_gpio) 
