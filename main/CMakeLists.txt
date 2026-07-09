idf_component_register(SRCS "smib_main.c"
                            "drivers/display.c"
                            "drivers/nfc.c"
                            "drivers/audio.c"
                            "mux/crc16.c"
                            "mux/mux.c"
                            "mux/log_bridge.c"
                            "app/app.c"
                            "cms/cms.c"
                            "sas/sas.c"
                            "ui/ui.c"
                            "ui/ui_idle.c"
                            "ui/ui_main.c"
                            "ui/ui_transfer.c"
                            "ui/ui_order.c"
                            "assets/Sf_Compact_14.c"
                            "assets/royal_club_zelenezburg_logo.c"
                       PRIV_REQUIRES
                       esp_timer
                       driver
                       esp_driver_uart
                       esp_driver_dac
                       json
                       lvgl
                       esp_lcd
                       spi_flash
                       INCLUDE_DIRS ".")
