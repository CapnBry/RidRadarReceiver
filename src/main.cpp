#include <Arduino.h>
#include "wifi_frame_parser.h"

#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft;
static uint32_t lastPilotUpdate;

static void wifi_promisc_callback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT)
        return;
    wifi_parse_pkt((wifi_promiscuous_pkt_t *)buf);
}

static void wifi_beacon_init()
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_NULL);
    esp_wifi_start();
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

    esp_wifi_set_promiscuous_rx_cb(&wifi_promisc_callback);
    esp_wifi_set_promiscuous(true);
}

static void tft_init()
{
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);
}

static void serial_dump_pilot(RadarPilotInfo *p)
{
    Serial.printf("%u ==== " MACSTR " ====\r\n", millis(), MAC2STR(p->mac));

    if (p->uas_data.BasicIDValid)
    {
        for (unsigned i=0; i<ODID_BASIC_ID_MAX_MESSAGES; ++i)
        {
            if (p->uas_data.BasicID[i].IDType != ODID_IDTYPE_NONE)
            {
                Serial.printf("BasicID(%u): [%d] %20s - %d\r\n", i,
                    p->uas_data.BasicID[i].IDType,
                    p->uas_data.BasicID[i].UASID,
                    p->uas_data.BasicID[i].UAType);
            }
        }
    }

    if (p->uas_data.OperatorIDValid)
    {
        Serial.printf("OperatorID: [%d] %s\r\n",
            p->uas_data.OperatorID.OperatorIdType,
            p->uas_data.OperatorID.OperatorId);
    }

    if (p->uas_data.LocationValid)
    {
        Serial.printf("Location: %.6f, %.6f Hdg: %d Alt: %d Spd: %.1f\r\n",
                p->uas_data.Location.Latitude,
                p->uas_data.Location.Longitude,
                (int32_t)p->uas_data.Location.Direction,
                (int32_t)p->uas_data.Location.Height,
                p->uas_data.Location.SpeedHorizontal);
    }

    if (p->uas_data.SystemValid)
    {
        Serial.printf("Operator Location: [%d] %.6f, %.6f\r\n",
                p->uas_data.System.OperatorLocationType,
                p->uas_data.System.OperatorLatitude,
                p->uas_data.System.OperatorLongitude);
    }
}

static void tft_show_pilot(RadarPilotInfo *p)
{
    tft.setTextFont(4);
    //tft.fillScreen(TFT_BLACK);
    uint16_t xpos = 0;
    uint16_t ypos = 0;

    // OperatorID
    if (p->uas_data.OperatorIDValid)
    {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString(p->uas_data.OperatorID.OperatorId, xpos, ypos);
    }

    xpos = 0;
    ypos += tft.fontHeight();

    // Location
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    xpos = tft.drawString("Pos:", xpos, ypos) + 5;
    if (p->uas_data.LocationValid)
    {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        const char *dirStr;
        double dirVal;
        if (p->uas_data.Location.Latitude < 0)
        {
            dirStr = "S";
            dirVal = -p->uas_data.Location.Latitude;
        }
        else
        {
            dirStr = "N";
            dirVal = p->uas_data.Location.Latitude;
        }
        tft.drawFloat(dirVal, 6, xpos, ypos);
        tft.drawString(dirStr, tft.width() - 25, ypos);

        //xpos = 0;
        ypos += tft.fontHeight();
        if (p->uas_data.Location.Longitude < 0)
        {
            dirStr = "W";
            dirVal = -p->uas_data.Location.Longitude;
        }
        else
        {
            dirStr = "E";
            dirVal = p->uas_data.Location.Longitude;
        }
        tft.drawFloat(dirVal, 6, xpos, ypos);
        tft.drawString(dirStr, tft.width() - 25, ypos);

        // Heading / Altitude line
        ypos += tft.fontHeight();
        tft.setTextFont(2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        xpos += tft.drawString("Alt", xpos, ypos) + 5;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        xpos += tft.drawNumber((int32_t)(p->uas_data.Location.Height), xpos, ypos);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("m", xpos, ypos);

        xpos = tft.width() / 2;
        xpos += tft.drawString("Hdg", xpos, ypos) + 5;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawNumber((int32_t)(p->uas_data.Location.Direction), xpos, ypos);

        xpos = tft.width() * 3 / 4;
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        xpos += tft.drawString("Spd", xpos, ypos) + 5;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        xpos += tft.drawNumber((int32_t)(p->uas_data.Location.SpeedHorizontal), xpos, ypos);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("m/s", xpos, ypos);
    }

    ypos = 96;
    uint16_t rssiWidth = map(constrain(p->rssi, -100, -50), -100, -50, 0, tft.width());
    tft.fillRect(0, ypos, rssiWidth, 12, TFT_DARKGREY);
    tft.fillRect(rssiWidth, ypos, tft.width() - rssiWidth, tft.fontHeight(), TFT_BLACK);

    // update date/time
    tft.setTextFont(2);
    ypos = tft.height() - tft.fontHeight();
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawNumber(lastPilotUpdate, 0, ypos);
}

static void tft_update_active()
{
    // If active in the last short time, draw a green square bottom right corner
    // if not, no square and dim the backlight
    bool active = (millis() - lastPilotUpdate < 1100);
    tft.fillRect(tft.width() - 12, tft.height() - 12, 12, 12, active ? TFT_GREEN : TFT_BLACK);
    analogWrite(TFT_BL, active ? 192 : 10);
}

static void pilot_dump()
{
    RadarPilotInfo pcopy = pilot;
    memset(&pilot, 0, sizeof(pilot));

    lastPilotUpdate = millis();
    serial_dump_pilot(&pcopy);
    tft_show_pilot(&pcopy);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("$UCID,RidRadarReceiver," __DATE__ " " __TIME__);

    tft_init();
    wifi_beacon_init();
}

void loop()
{
    if (pilot.dirty)
        pilot_dump();
    tft_update_active();
    delay(10);
}
