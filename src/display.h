#pragma once

#include <TFT_eSPI.h>
#include "rid_tracker.h"

// All drawing goes to target, which must already be initialized and rotated
void display_init(TFT_eSPI *target);
// Draw the pilot below the header, lastUpdate is shown on the status line
void display_show_pilot(RadarPilotInfo *p, uint32_t lastUpdate);
// Clear everything but the status line at the bottom
void display_clear_pilot();
// Green square in the bottom right corner when recently active
void display_show_active(bool active);
void display_show_vbat(uint32_t vbat);
