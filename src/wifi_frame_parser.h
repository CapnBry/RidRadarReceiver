#pragma once

#include <opendroneid.h>
#include <esp_wifi.h>

typedef struct tagRadarPilotInfo {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t dirty;
    ODID_UAS_Data uas_data;
} RadarPilotInfo;

void wifi_parse_pkt(wifi_promiscuous_pkt_t *pkt);

extern RadarPilotInfo pilot;