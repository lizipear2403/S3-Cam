idf_component_register(SRCS S3-Cam.c
                        INCLUDE_DIRS "."
                        PRIV_INCLUDE_DIRS .
                        PRIV_REQUIRES esp_psram
                                    esp_driver_sdmmc
                                    sdmmc
                                    fatfs
                                    esp_driver_spi
                                    esp_driver_gpio
                                )
