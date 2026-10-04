#include <string.h>
#include "rid_tracker.h"

static RadarPilotInfo pilots[RID_MAX_PILOTS];

static RadarPilotInfo *rid_tracker_find_or_add(const uint8_t mac[6], uint32_t now)
{
    RadarPilotInfo *slot = nullptr;
    for (unsigned i=0; i<RID_MAX_PILOTS; ++i)
    {
        RadarPilotInfo *p = &pilots[i];
        if (p->inUse && memcmp(p->mac, mac, sizeof(p->mac)) == 0)
            return p;
        // Prefer an empty slot, otherwise the one seen longest ago
        if (!slot || (slot->inUse && (!p->inUse || now - p->lastSeen > now - slot->lastSeen)))
            slot = p;
    }

    memset(slot, 0, sizeof(*slot));
    odid_initUasData(&slot->uas_data);
    memcpy(slot->mac, mac, sizeof(slot->mac));
    slot->inUse = 1;
    // Flush the first update right away
    slot->lastFlush = now - RID_FLUSH_INTERVAL_MS;
    return slot;
}

RadarPilotInfo *rid_tracker_process(const RidRawMessage *msg, uint32_t now)
{
    RadarPilotInfo *p = rid_tracker_find_or_add(msg->mac, now);
    for (unsigned i=0; i<msg->count; ++i)
        decodeOpenDroneID(&p->uas_data, (uint8_t *)msg->msgs[i]);

    p->rssi = msg->rssi;
    p->source = msg->source;
    p->lastSeen = now;
    p->dirty = 1;
    return p;
}

void rid_tracker_flush(uint32_t now, void (*cb)(RadarPilotInfo *p))
{
    for (unsigned i=0; i<RID_MAX_PILOTS; ++i)
    {
        RadarPilotInfo *p = &pilots[i];
        if (p->dirty && now - p->lastFlush >= RID_FLUSH_INTERVAL_MS)
        {
            p->dirty = 0;
            p->lastFlush = now;
            cb(p);
        }
    }
}
