#include <algorithm>

#include <Arduino.h>
#include "wifi_frame_parser.h"
#include <odid_wifi.h>

#define RADAR_ID_LEN 20

#define IEEE80211_ELEMID_SSID		0x00
#define IEEE80211_ELEMID_RATES		0x01
#define IEEE80211_ELEMID_VENDOR		0xDD

RadarPilotInfo pilot;

static void hexdump(void *p, size_t len)
{
    char *data = (char *)p;
    while (len > 0)
    {
        uint8_t linepos = 0;
        char* linestart = data;
        // Binary part
        while (len > 0 && linepos < 16)
        {
            if (*data < 0x0f)
            Serial.write('0');
            Serial.print(*data, HEX);
            Serial.write(' ');
            ++data;
            ++linepos;
            --len;
        }
         // Spacer to align last line
         for (uint8_t i = linepos; i < 16; ++i)
             Serial.print("   ");

         // ASCII part
         for (uint8_t i = 0; i < linepos; ++i)
             Serial.write((linestart[i] < ' ') ? '.' : linestart[i]);
         Serial.println();
     }
}

void wifi_parse_pkt(wifi_promiscuous_pkt_t *pkt)
{
    ieee80211_mgmt *hdr = (ieee80211_mgmt *)pkt->payload;

    // Looking for a beacon frame, 0x8000
    // MAC of transmitter is in hdr.sa
    if (hdr->frame_control != htons(0x8000))
        return;
    size_t payloadlen = pkt->rx_ctrl.sig_len; // len - sizeof(wifi_pkt_rx_ctrl_t);
    size_t offset = sizeof(ieee80211_mgmt) + sizeof(ieee80211_beacon);
    bool found_odid = false;
    char *ssid;
    size_t ssid_len;

    while (offset < payloadlen && !found_odid)
    {
        switch (pkt->payload[offset]) // ieee80211 element_id
        {
        case IEEE80211_ELEMID_SSID:
            {
                // Operator ID is in the SSID field
                ieee80211_ssid *iessid = (ieee80211_ssid *)&pkt->payload[offset];
                ssid_len = std::min((uint8_t)RADAR_ID_LEN, iessid->length);
                ssid = (char *)iessid->ssid;
                offset += sizeof(ieee80211_ssid) + iessid->length;
                break;
            }
        case IEEE80211_ELEMID_RATES:
            {
                ieee80211_supported_rates *iesr = (ieee80211_supported_rates *)&pkt->payload[offset];
                // ODID beacon reports 1 rate of 6mbit
                if (iesr->length > 1 || iesr->supported_rates != 0x8C)
                    return;
                offset += sizeof(ieee80211_supported_rates);
                break;
            }
        case IEEE80211_ELEMID_VENDOR:
            {
                ieee80211_vendor_specific *ievs = (ieee80211_vendor_specific *)&pkt->payload[offset];
                // The OUI of opendroneid is FA-0B-BC
                if (ievs->oui[0] != 0xfa || ievs->oui[1] != 0x0b || ievs->oui[2] != 0xbc || ievs->oui_type != 0x0d)
                    return;
                offset += sizeof(ieee80211_vendor_specific);
                found_odid = true;
                break;
            }
        default:
            // Any unexpected element_id means we can pound sand
            return;
        } // switch
    } // while (<len)

    if (!found_odid)
        return;

    //hexdump(&pkt->payload[offset], payloadlen - offset);

    //ODID_service_info *si = (struct ODID_service_info *)&pkt->payload[offset];
    // can use si->counter to maybe do link quality, by detecting missing packet
    offset += sizeof(ODID_service_info);

    ODID_MessagePack_encoded *msg_pack_enc = (ODID_MessagePack_encoded *)&pkt->payload[offset];
    size_t expected_size = sizeof(ODID_MessagePack_encoded) - ODID_MESSAGE_SIZE * (ODID_PACK_MAX_MESSAGES - msg_pack_enc->MsgPackSize);
    if (expected_size < payloadlen - offset
        && decodeMessagePack(&pilot.uas_data, msg_pack_enc) == ODID_SUCCESS)
    {
        memcpy(pilot.mac, hdr->sa, sizeof(pilot.mac));
        pilot.rssi = pkt->rx_ctrl.rssi;
        pilot.dirty = 1;
    }
}
