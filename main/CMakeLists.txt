set(srcs "smib_main.c"
         "drivers/nfc.c"
         "drivers/net_wifi.c"
         "drivers/settings_nvs.c"
         "drivers/door.c"
         "mux/crc16.c"
         "mux/mux_frame.c"
         "mux/mux.c"
         "mux/log_bridge.c"
         "app/app.c"
         "cms/cms.c"
         "cms/cms_link.c"
         "sas/sas.c"
         "ui/ui.c"
         "ui/ui_idle.c"
         "ui/ui_main.c"
         "ui/ui_transfer.c"
         "ui/ui_order.c"
         "ui/ui_order_status.c"
         "ui/ui_admin.c"
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
                       esp_driver_gpio
                       ${board_reqs}
                       json
                       lvgl
                       esp_lcd
                       spi_flash
                       # Wi-Fi carries the CMS channel; NVS holds the settings
                       # the admin panel writes.
                       esp_wifi
                       esp_netif
                       esp_event
                       nvs_flash
                       lwip
                       INCLUDE_DIRS ".")

# Admin passcode and the factory Wi-Fi/CMS defaults, read from ../.env.
get_filename_component(SMIB_ENV_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${SMIB_ENV_DIR}/cmake/smib_env.cmake")
smib_env_read(SMIB_ENV_DEFS)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${SMIB_ENV_DEFS})
