#pragma once

#include <esp_wifi.h>

// Called from the WiFi task, valid RemoteID frames are pushed to the rid_queue
void wifi_parse_pkt(wifi_promiscuous_pkt_t *pkt);