// Redirects esp_log output into mux LOG frames (channel 0x7F) so firmware
// logs share UART0 with SAS/CMS traffic without corrupting frames.
#pragma once

// Call once, after mux_init(). Boot output before this point goes nowhere
// (console is CONFIG_ESP_CONSOLE_NONE).
void log_bridge_install(void);
