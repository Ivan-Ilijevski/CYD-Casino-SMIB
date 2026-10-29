// drivers/door.h on the desktop: the cabinet door switch on a keyboard key.
//
// Hold `D` to hold the door open, or latch it with `door open` / `door closed`
// on the control FIFO so a script can drive the admin panel. No debouncing to
// simulate — a keypress has none of a microswitch's bounce, and the firmware's
// debouncer is what is being stood in for, not tested.

#include <string.h>

#include "drivers/door.h"
#include "sim.h"

static bool s_key_held;     // `D` is down
static bool s_latched;      // `door open` from the console

esp_err_t door_init(void)
{
    sim_printf("door switch: hold D, or `door open|closed` on the ctl FIFO");
    return ESP_OK;
}

bool door_is_open(void)
{
    return s_key_held || s_latched;
}

// ---------------------------------------------------------------- sim API

void sim_door_key(bool down)
{
    if (down == s_key_held) {
        return;
    }
    s_key_held = down;
    sim_printf("door %s (D key)", door_is_open() ? "OPEN" : "shut");
}

bool sim_door_command(const char *arg)
{
    if (strcmp(arg, "open") == 0) {
        s_latched = true;
    } else if (strcmp(arg, "closed") == 0 || strcmp(arg, "close") == 0) {
        s_latched = false;
    } else {
        sim_printf("usage: door open|closed");
        return false;
    }
    sim_printf("door %s (latched)", door_is_open() ? "OPEN" : "shut");
    return true;
}

const char *sim_door_status(void)
{
    return door_is_open() ? "OPEN" : "shut";
}
