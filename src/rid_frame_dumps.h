#pragma once

#include "wifi_frame_parser.h"

// Dump everything in the packet
void serial_dump_pilot(RadarPilotInfo *p);
// Individual section dumpers
void serial_dump_basicids(RadarPilotInfo *p);
void serial_dump_opid(RadarPilotInfo *p);
void serial_dump_location(RadarPilotInfo *p);
void serial_dump_system(RadarPilotInfo *p);