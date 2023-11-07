#include <Arduino.h>
#include "targets.h"
#include "wifi_frame_parser.h"
#include "rid_frame_dumps.h"

#include <TFT_eSPI.h>
#include <SPI.h>
#include <button.h>

TFT_eSPI tft;
#if defined(PIN_BUTTON0)
static Button btn0;
#endif
#if defined(HAS_TEMPERATURE)
//static temperature_sensor_handle_t hTemp;
#endif
#if defined(PIN_AMBIENTLIGHT)
static enum eLightLevel { llDay, llNight } g_LightLevel;
#endif
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

static void tft_aw9364_reduce_duty(uint8_t cnt)
{
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
    // AW9364 uses pulses to change LED current
    // High is recommended >0.5us and low is 0.5us < Low < 500us
    while (cnt--)
    {
        digitalWrite(TFT_BL, LOW);
        delayMicroseconds(5);
        digitalWrite(TFT_BL, HIGH);
        if (cnt)
            delayMicroseconds(5);
    }
#endif
}

static void tft_aw9364_set_duty(uint8_t duty)
{
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
    // duty is 0-15 and starts at 15
    static uint8_t current = 15;

    uint8_t steps;
    if (duty <= current)
        steps = current - duty;
    else
        steps = current + 16 - duty;

    current = duty;
    if (steps > 0)
        tft_aw9364_reduce_duty(steps);
#endif
}

static void tft_set_backlight(bool high)
{
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
    // Low is a 0/15 duty cycle and high is 15/15 duty cycle
    // but high can be adjusted down if ambient light is low
    if (high)
    {
        uint8_t level;
#if defined(PIN_AMBIENTLIGHT)
        if (g_LightLevel == llNight)
            level = 6;
        else
#endif
            level = 15;
        tft_aw9364_set_duty(level);
    }
    else
        tft_aw9364_set_duty(0);
#else
    analogWrite(TFT_BL, high ? 192 : 10);
#endif
}

static void tft_init()
{
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, TFT_BACKLIGHT_ON);

    tft.init();
    tft.setRotation(1); // 3 to flip
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false, false);

    tft.setTextFont(4);
    tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    tft.drawString("RidRadarReceiver", 0, 0);
}

static void tft_show_pilot_opid(RadarPilotInfo *p, uint16_t xpos, uint16_t ypos)
{
    char mac[19];
    char *opid = nullptr;
    if (p->uas_data.OperatorIDValid)
    {
        opid = p->uas_data.OperatorID.OperatorId;
    }
    else if (p->uas_data.BasicIDValid)
    {
        for (unsigned i=0; i<ODID_BASIC_ID_MAX_MESSAGES; ++i)
        {
            if (p->uas_data.BasicID[i].IDType != ODID_IDTYPE_NONE)
            {
                opid = p->uas_data.BasicID[i].UASID;
                break;
            }
        }
    }
    if (!opid)
    {
        snprintf(mac, sizeof(mac), MACSTR, MAC2STR(p->mac));
        opid = mac;
        //tft.fillRect(0, 0, tft.width(), tft.fontHeight(), TFT_BLACK);
    }

    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString(opid, xpos, ypos);
}

static void tft_show_pilot(RadarPilotInfo *p)
{
    tft.setTextFont(4);
    //tft.fillScreen(TFT_BLACK);
    uint16_t xpos = 0;
    uint16_t ypos = 0;

    // OperatorID
    tft_show_pilot_opid(p, xpos, ypos);

    xpos = 0;
    ypos += tft.fontHeight();

    // Location
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    xpos = tft.drawString("Pos", xpos, ypos) + 5;
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

    // RSSI and bar
    ypos = 96;
    tft.setTextFont(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.drawNumber(p->rssi, 0, ypos - tft.fontHeight() - 2);
    uint16_t rssiWidth = map(constrain(p->rssi, -95, -75), -95, -75, 0, tft.width());
    tft.fillRect(0, ypos, rssiWidth, 12, TFT_DARKGREY);
    tft.drawRect(0, ypos, rssiWidth, 12, TFT_ORANGE);
    tft.fillRect(rssiWidth, ypos, tft.width() - rssiWidth, tft.fontHeight(), TFT_BLACK);

    // update date/time
    ypos = tft.height() - tft.fontHeight();
    tft.setTextFont(2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawNumber(lastPilotUpdate, 0, ypos);
}

static void tft_update_active(uint32_t now)
{
    // If active in the last short time, draw a green square bottom right corner. If not, no square
    bool active = (now - lastPilotUpdate < 2100);
    tft.fillRect(tft.width() - 12, tft.height() - 12, 12, 12, active ? TFT_GREEN : TFT_BLACK);
    // High backlight if any activity in the last hour
    tft_set_backlight(lastPilotUpdate && (now - lastPilotUpdate) < (1000U * 60U * 60U));
}

static void tft_update_vbat(uint32_t vbat)
{
    tft.setTextFont(2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);

    int32_t ypos = tft.height() - tft.fontHeight();
    int32_t xpos = tft.width() / 2;
    tft.fillRect(xpos - 24, ypos, 54, tft.fontHeight(), TFT_BLACK);
    xpos += (tft.drawNumber(vbat, xpos, ypos) / 2);

    tft.setTextDatum(TL_DATUM);
    tft.drawChar('V', xpos, ypos);
}

static void btn_update()
{
#if defined(PIN_BUTTON0)
    btn0.update();
#endif
}

static void ambientlight_update(uint32_t now)
{
#if defined(PIN_AMBIENTLIGHT)
    static uint32_t last;
    constexpr uint32_t AMBIENTLIGHT_INTERVAL = 500;
    if (last && now - last < AMBIENTLIGHT_INTERVAL)
        return;
    last = now;

    // Ambient light sensor is just an LED connected between then analog pin
    // and GND. Generates enough voltage in light to be able to determine if
    // it is dark or bright
    uint32_t adc = analogRead(PIN_AMBIENTLIGHT);
    if (adc > 1200)
        g_LightLevel = llDay;
    else
        g_LightLevel = llNight;
    //Serial.printf("LightLevel=%u ADC=%u\n", (uint8_t)g_LightLevel, adc);
#endif
}

static void vbat_update(uint32_t now)
{
#if defined(PIN_VBAT)
    static uint32_t last;
    constexpr uint32_t VBAT_INTERVAL = 500;
    if (last && now - last < VBAT_INTERVAL)
        return;
    last = now;

    uint32_t mv = analogReadMilliVolts(PIN_VBAT);

    // TODO: Calibrate battery
    tft_update_vbat(mv);
#endif
}

static void temp_update(uint32_t now)
{
#if defined(HAS_TEMPERATURE)
    static uint32_t last;
    constexpr uint32_t TEMP_INTERVAL = 1000;
    if (last && now - last < TEMP_INTERVAL)
        return;
    last = now;

    // float tsens_out;
    // temperature_sensor_get_celsius(hTemp, &tsens_out);
#endif
}

static void pilot_dump(uint32_t now)
{
    RadarPilotInfo pcopy = pilot;
    memset(&pilot, 0, sizeof(pilot));

    lastPilotUpdate = now;
    serial_dump_pilot(&pcopy);
    tft_show_pilot(&pcopy);
}

static void hardware_init()
{
#if defined(PIN_BUTTON0)
    btn0.init(PIN_BUTTON0, false);
    btn0.OnShortPress = []() { lastPilotUpdate = millis(); };
#endif
#if defined(ARDUINO_LILYGO_T_DISPLAY_S3)
    // Soft power switch must be on
    #define PIN_LCD_POWER_ON 15
    pinMode(PIN_LCD_POWER_ON, OUTPUT);
    digitalWrite(PIN_LCD_POWER_ON, HIGH);
#endif
}

void setup()
{
    Serial.begin(115200);
    Serial.println("$UCID,RidRadarReceiver," __DATE__ " " __TIME__);

    hardware_init();
    tft_init();
    wifi_beacon_init();
}

void loop()
{
    uint32_t now = millis();

    if (pilot.dirty)
        pilot_dump(now);
    ambientlight_update(now);
    tft_update_active(now);
    btn_update();
    vbat_update(now);

    delay(10);
}
