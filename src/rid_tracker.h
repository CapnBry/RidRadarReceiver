#pragma once

#include <opendroneid.h>
#include "rid_queue.h"

#define RID_MAX_PILOTS 8
// Minimum time between flushes of the same pilot
#define RID_FLUSH_INTERVAL_MS 500

typedef struct tagRadarPilotInfo {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t source; // RidSource of the last update
    uint8_t inUse;
    uint8_t dirty;
    uint32_t lastSeen;
    uint32_t lastFlush;
    ODID_UAS_Data uas_data;
} RadarPilotInfo;

// Decode msg into the matching pilot (adding one if needed, evicting the
// least recently seen if full) and return it. Only call from the main loop.
RadarPilotInfo *rid_tracker_process(const RidRawMessage *msg, uint32_t now);
// Call cb for each pilot updated since its last flush, at most once per
// RID_FLUSH_INTERVAL_MS per pilot, then clear dirty
void rid_tracker_flush(uint32_t now, void (*cb)(RadarPilotInfo *p));
