#pragma once

#include <stdint.h>

// Creates the full-screen video background and the single BPM numeral.
// Must be called while holding the LVGL lock.
void heart_ui_create(void);

// Updates the numeral. 0 shows the "--" placeholder.
void heart_ui_set_bpm(uint16_t bpm);
