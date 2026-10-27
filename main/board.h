// Board selection for the SMIB. The board is picked in menuconfig
// ("SMIB board"), defaulting from the IDF target; each board header defines
// the same pin/resolution macros the drivers and mux use.
#pragma once

#include "sdkconfig.h"

#define SMIB_FW_VERSION     "0.1.0"
#define SMIB_DEVICE_ID      "smib-1"

#if defined(CONFIG_SMIB_BOARD_ES3C35P)
#include "boards/board_es3c35p.h"
#else // E32R40T; also the desktop sim, whose sdkconfig.h has no board symbol
#include "boards/board_e32r40t.h"
#endif
