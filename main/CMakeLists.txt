set(srcs "smib_main.c"
         "drivers/nfc.c"
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
         "assets/royal_club_zelenezburg_logo_transparent.c")

# Display + audio drivers per board (Kconfig "SMIB board"); both implement
# drivers/display.h and drivers/audio.h.
if(CONFIG_SMIB_BOARD_ES3C35P)
    list(APPEND srcs "drivers/display_st77922.c" "drivers/audio_i2s.c" "drivers/i2c_bus.c")
    set(board_reqs esp_driver_i2c esp_driver_i2s)
else()
    list(APPEND srcs "drivers/display_st7796.c" "drivers/audio_ledc.c")
    set(board_reqs esp_driver_ledc)
endif()

idf_component_register(SRCS ${srcs}
                       PRIV_REQUIRES
                       esp_timer
                       driver
                       esp_driver_uart
                       ${board_reqs}
                       json
                       lvgl
                       esp_lcd
                       spi_flash
                       INCLUDE_DIRS ".")
