#include <Arduino.h>
#include "targets.h"
#include "wifi_frame_parser.h"
#include "rid_frame_dumps.h"
#include "rid_queue.h"
#include "rid_tracker.h"
#include "ble_scanner.h"

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
// Pilot on the display, kept until it hasn't been heard for DISPLAY_HOLD_MS
static RadarPilotInfo *shownPilot;
#define DISPLAY_HOLD_MS 10000
// Pixel widths of the number fields at their widest value, measured once.
// The fonts are proportional but all digits in a font are the same width
static int16_t fwCoord, fwAlt, fwHdgSpd, fwRssi;

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
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

    tft.init();
    fwCoord = tft.textWidth("888.888888", 4);
    fwAlt = tft.textWidth("-8888", 2);
    fwHdgSpd = tft.textWidth("888", 2);
    fwRssi = tft.textWidth("-888", 2);
    tft.setRotation(1); // 3 to flip
    tft.fillScreen(TFT_BLACK);
    tft.setTextWrap(false, false);

    tft.setTextFont(4);
    tft.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    tft.drawString("RidRadarReceiver", 0, 0);
}

// Source icons are h tall (the header line) and this wide
static int32_t tft_icon_width(int32_t h)
{
    return h * 4 / 3;
}

// Bluetooth logo, white rune on a blue oval
static void tft_draw_icon_bluetooth(int32_t x, int32_t y, int32_t h)
{
    const uint32_t btBlue = tft.color565(0x00, 0x82, 0xfc);
    int32_t w = tft_icon_width(h);
    tft.fillRect(x, y, w, h, TFT_BLACK);

    int32_t bw = h * 2 / 3;
    int32_t bx = x + (w - bw) / 2;
    tft.fillSmoothRoundRect(bx, y, bw, h, bw / 2, btBlue, TFT_BLACK);

    // The rune is one line: lower-left arm, up to the bottom point, down the
    // stem, top point, then the upper-left arm
    float cx = bx + bw / 2.0f;
    float y0 = y + h * 0.18f;
    float y1 = y + h * 0.82f;
    float q = (y1 - y0) / 4;
    const float pts[][2] = {
        { cx - q, y0 + q }, { cx + q, y1 - q }, { cx, y1 },
        { cx, y0 }, { cx + q, y0 + q }, { cx - q, y1 - q },
    };
    float wd = h / 12.0f;
    for (unsigned i=0; i<(sizeof(pts) / sizeof(pts[0])) - 1; ++i)
        tft.drawWideLine(pts[i][0], pts[i][1], pts[i+1][0], pts[i+1][1], wd, TFT_WHITE, btBlue);
}

// WiFi signal indicator, square dot at the bottom center with arcs above it
static void tft_draw_icon_wifi(int32_t x, int32_t y, int32_t h)
{
    int32_t w = tft_icon_width(h);
    tft.fillRect(x, y, w, h, TFT_BLACK);

    int32_t t = (h + 5) / 9; // arc thickness
    int32_t dot = t + 1;
    int32_t cx = x + w / 2;
    int32_t cy = y + h - 1 - dot / 2;
    tft.fillRect(cx - dot / 2, y + h - dot, dot, dot, TFT_GREEN);

    // Three 90 degree arcs centered on the dot, the outer one reaching the
    // top. Arc angles start at 6 o'clock going clockwise so 180 is straight up
    int32_t rmax = cy - y;
    for (int32_t i=1; i<=3; ++i)
    {
        int32_t r = rmax * i / 3;
        tft.drawSmoothArc(cx, cy, r, r - t, 135, 225, TFT_GREEN, TFT_BLACK, true);
    }
}

static void tft_show_pilot_opid(RadarPilotInfo *p, uint16_t xpos, uint16_t ypos)
{
    char mac[19];
    char *opid = nullptr;
    // Operator ID is optional and often sent empty (US), so fall back to the
    // CAA registration ID, then any other basic ID (serial number etc)
    if (p->uas_data.OperatorIDValid && p->uas_data.OperatorID.OperatorId[0])
        opid = p->uas_data.OperatorID.OperatorId;
    for (unsigned pass=0; pass<2 && !opid; ++pass)
    {
        for (unsigned i=0; i<ODID_BASIC_ID_MAX_MESSAGES; ++i)
        {
            ODID_BasicID_data *bid = &p->uas_data.BasicID[i];
            if (!p->uas_data.BasicIDValid[i] || bid->IDType == ODID_IDTYPE_NONE || !bid->UASID[0])
                continue;
            if (pass == 0 && bid->IDType != ODID_IDTYPE_CAA_REGISTRATION_ID)
                continue;
            opid = bid->UASID;
            break;
        }
    }
    if (!opid)
    {
        snprintf(mac, sizeof(mac), MACSTR, MAC2STR(p->mac));
        opid = mac;
        //tft.fillRect(0, 0, tft.width(), tft.fontHeight(), TFT_BLACK);
    }

    // Header line: ID, then the source icon right aligned
    int32_t h = tft.fontHeight();
    int32_t iconX = tft.width() - tft_icon_width(h);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    xpos += tft.drawString(opid, xpos, ypos);
    if (xpos < iconX)
        tft.fillRect(xpos, ypos, iconX - xpos, h, TFT_BLACK);

    if (p->source == RID_SRC_BLE4)
        tft_draw_icon_bluetooth(iconX, ypos, h);
    else
        tft_draw_icon_wifi(iconX, ypos, h);
}

// Clear everything but the status line at the bottom
static void tft_clear_pilot()
{
    tft.fillRect(0, 0, tft.width(), tft.height() - tft.fontHeight(2), TFT_BLACK);
}

// Draw n right aligned in a field w wide, padding covers the previous value.
// Unknown values are shown as dashes
static int32_t tft_draw_number_field(int32_t n, bool valid, int32_t x, int32_t y, int16_t w)
{
    tft.setTextDatum(TR_DATUM);
    tft.setTextPadding(w);
    if (valid)
        tft.drawNumber(n, x + w, y);
    else
        tft.drawString("---", x + w, y);
    tft.setTextPadding(0);
    tft.setTextDatum(TL_DATUM);
    return w;
}

// Draw one coordinate, the value then the hemisphere letter at the right edge
static void tft_draw_coord(double val, bool valid, const char *pos, const char *neg, int32_t x, int32_t y)
{
    tft.setTextPadding(fwCoord);
    if (valid)
        tft.drawFloat(val < 0 ? -val : val, 6, x, y);
    else
        tft.drawString("---", x, y);

    int32_t hx = tft.width() - 25;
    tft.setTextPadding(tft.width() - hx);
    if (valid)
        tft.drawString(val < 0 ? neg : pos, hx, y);
    else
        tft.fillRect(hx, y, tft.width() - hx, tft.fontHeight(), TFT_BLACK);
    tft.setTextPadding(0);
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
        const ODID_Location_data *loc = &p->uas_data.Location;
        bool posValid = rid_latlon_valid(loc->Latitude, loc->Longitude);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_draw_coord(loc->Latitude, posValid, "N", "S", xpos, ypos);
        ypos += tft.fontHeight();
        tft_draw_coord(loc->Longitude, posValid, "E", "W", xpos, ypos);

        // Heading / Altitude line
        ypos += tft.fontHeight();
        tft.setTextFont(2);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        xpos += tft.drawString("Alt", xpos, ypos) + 5;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        xpos += tft_draw_number_field((int32_t)loc->Height, rid_alt_valid(loc->Height), xpos, ypos, fwAlt) + 2;
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("m", xpos, ypos);

        xpos = tft.width() / 2;
        xpos += tft.drawString("Hdg", xpos, ypos) + 5;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft_draw_number_field((int32_t)loc->Direction, rid_dir_valid(loc->Direction), xpos, ypos, fwHdgSpd);

        xpos = tft.width() * 3 / 4;
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        xpos += tft.drawString("Spd", xpos, ypos) + 5;
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        xpos += tft_draw_number_field((int32_t)loc->SpeedHorizontal, rid_speed_valid(loc->SpeedHorizontal), xpos, ypos, fwHdgSpd) + 2;
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString("m/s", xpos, ypos);
    }

    // RSSI and bar
    ypos = 96;
    tft.setTextFont(2);
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    // Pad to the widest value so a shorter number covers the previous one
    tft.setTextPadding(fwRssi);
    tft.drawNumber(p->rssi, 0, ypos - tft.fontHeight() - 2);
    tft.setTextPadding(0);
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

static void rid_process_queue(uint32_t now)
{
    RidRawMessage msg;
    bool shownUpdated = false;
    while (rid_queue_receive(&msg))
    {
        RadarPilotInfo *p = rid_tracker_process(&msg, now);
        lastPilotUpdate = now;

        // Switch to another pilot only once the shown one goes quiet
        if (p != shownPilot)
        {
            if (shownPilot && now - shownPilot->lastSeen < DISPLAY_HOLD_MS)
                continue;
            shownPilot = p;
            tft_clear_pilot();
        }
        shownUpdated = true;
    }

    rid_tracker_flush(now, serial_dump_pilot);
    if (shownUpdated)
        tft_show_pilot(shownPilot);
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

    rid_queue_init();
    hardware_init();
    tft_init();
    wifi_beacon_init();
    ble_scanner_init();
}

void loop()
{
    uint32_t now = millis();

    rid_process_queue(now);
    ambientlight_update(now);
    tft_update_active(now);
    btn_update();
    vbat_update(now);

    delay(10);
}
