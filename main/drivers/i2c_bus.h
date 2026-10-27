// Shared I2C master bus (ES3C35P: touch controller + ES8311 codec on IO38/39).
#pragma once

#include "driver/i2c_master.h"

// Creates the bus on first use; safe to call from display and audio init.
i2c_master_bus_handle_t i2c_bus_get(void);
