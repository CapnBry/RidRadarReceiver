#include <Arduino.h>
#include "targets.h"
#include "wifi_frame_parser.h"
#include "rid_frame_dumps.h"
#include "rid_queue.h"
#include "rid_tracker.h"
#include "ble_scanner.h"
#include "display.h"

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
    tft.setRotation(1); // 3 to flip
    display_init(&tft);
}

static void tft_update_active(uint32_t now)
{
    // If active in the last short time, draw a green square bottom right corner. If not, no square
    display_show_active(now - lastPilotUpdate < 2100);
    // High backlight if any activity in the last hour
    tft_set_backlight(lastPilotUpdate && (now - lastPilotUpdate) < (1000U * 60U * 60U));
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
    display_show_vbat(mv);
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
            display_clear_pilot();
        }
        shownUpdated = true;
    }

    rid_tracker_flush(now, serial_dump_pilot);
    if (shownUpdated)
        display_show_pilot(shownPilot, lastPilotUpdate);
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
