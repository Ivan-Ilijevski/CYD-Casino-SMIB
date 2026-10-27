#include "esp_err.h"

#include "board.h"
#include "i2c_bus.h"

static i2c_master_bus_handle_t s_bus;

i2c_master_bus_handle_t i2c_bus_get(void)
{
    // Only called from app_main during boot, so no locking.
    if (!s_bus) {
        i2c_master_bus_config_t cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = PIN_NUM_I2C_SDA,
            .scl_io_num = PIN_NUM_I2C_SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true, // board has 10K pull-ups too
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
    }
    return s_bus;
}
