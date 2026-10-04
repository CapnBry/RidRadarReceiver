// Host build that renders the display code into a sprite with sample data and
// writes each screen to a PNG, so layout changes can be checked without the
// hardware. Run with: pio run -e native -t exec
// The IDs and positions below are made up.
#include <TFT_eSPI.h>
#include <SPI.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "display.h"

SPIClass SPI;

#define OUT_DIR "sim/out"
#define PNG_SCALE 2

struct Screen {
    const char *name;
    int16_t width, height;
};

static const Screen screens[] = {
    { "tdisplay_s3", 320, 170 },
    { "tdisplay", 240, 135 },
};

struct Scene {
    const char *name;
    RadarPilotInfo pilot;
    bool hasPilot;
};

static uint32_t crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc = ~crc;
    while (len--)
    {
        crc ^= *buf++;
        for (int k=0; k<8; ++k)
            crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return ~crc;
}

static void put32(std::vector<uint8_t> &v, uint32_t x)
{
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

static void png_chunk(FILE *f, const char *type, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> c;
    put32(c, data.size());
    c.insert(c.end(), type, type + 4);
    c.insert(c.end(), data.begin(), data.end());
    uint32_t crc = crc32(0, &c[4], c.size() - 4);
    put32(c, crc);
    fwrite(c.data(), 1, c.size(), f);
}

// Write RGB rows as an uncompressed (stored deflate) PNG
static bool png_write(const char *path, int w, int h, const std::vector<uint8_t> &rgb)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    static const uint8_t sig[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    fwrite(sig, 1, sizeof(sig), f);

    std::vector<uint8_t> ihdr;
    put32(ihdr, w);
    put32(ihdr, h);
    ihdr.insert(ihdr.end(), { 8, 2, 0, 0, 0 }); // 8 bit RGB
    png_chunk(f, "IHDR", ihdr);

    // Each row is filter byte 0 then the pixels
    std::vector<uint8_t> raw;
    for (int y=0; y<h; ++y)
    {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + y * w * 3, rgb.begin() + (y + 1) * w * 3);
    }

    std::vector<uint8_t> z = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw)
    {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t pos=0; pos<raw.size(); )
    {
        size_t n = std::min(raw.size() - pos, (size_t)65535);
        z.push_back(pos + n == raw.size());
        z.push_back(n); z.push_back(n >> 8);
        z.push_back(~n); z.push_back(~n >> 8);
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    put32(z, (b << 16) | a);
    png_chunk(f, "IDAT", z);
    png_chunk(f, "IEND", {});
    return fclose(f) == 0;
}

static bool sprite_to_png(TFT_eSprite &spr, const char *path)
{
    int w = spr.width() * PNG_SCALE;
    int h = spr.height() * PNG_SCALE;
    std::vector<uint8_t> rgb(w * h * 3);
    for (int y=0; y<h; ++y)
    {
        for (int x=0; x<w; ++x)
        {
            uint16_t c = spr.readPixel(x / PNG_SCALE, y / PNG_SCALE);
            uint8_t *p = &rgb[(y * w + x) * 3];
            p[0] = ((c >> 11) & 0x1f) * 255 / 31;
            p[1] = ((c >> 5) & 0x3f) * 255 / 63;
            p[2] = (c & 0x1f) * 255 / 31;
        }
    }
    return png_write(path, w, h, rgb);
}

static void set_mac(RadarPilotInfo &p, uint8_t last)
{
    // Locally administered example address
    const uint8_t mac[6] = { 0x02, 0x00, 0x5e, 0x00, 0x00, last };
    memcpy(p.mac, mac, sizeof(p.mac));
}

static void set_basic_id(RadarPilotInfo &p, unsigned idx, ODID_idtype_t type, const char *id)
{
    p.uas_data.BasicIDValid[idx] = 1;
    p.uas_data.BasicID[idx].IDType = type;
    p.uas_data.BasicID[idx].UAType = ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
    strncpy(p.uas_data.BasicID[idx].UASID, id, ODID_ID_SIZE);
}

static void set_location(RadarPilotInfo &p, double lat, double lon, float alt, float hdg, float spd)
{
    p.uas_data.LocationValid = 1;
    p.uas_data.Location.Latitude = lat;
    p.uas_data.Location.Longitude = lon;
    p.uas_data.Location.Height = alt;
    p.uas_data.Location.Direction = hdg;
    p.uas_data.Location.SpeedHorizontal = spd;
}

static std::vector<Scene> make_scenes()
{
    std::vector<Scene> scenes;
    Scene s;

    // Boot screen before any pilot is heard
    memset(&s, 0, sizeof(s));
    s.name = "startup";
    scenes.push_back(s);

    // WiFi beacon, moving, all values known
    memset(&s, 0, sizeof(s));
    odid_initUasData(&s.pilot.uas_data);
    s.name = "wifi_moving";
    s.hasPilot = true;
    set_mac(s.pilot, 0x01);
    s.pilot.source = RID_SRC_WIFI_BEACON;
    s.pilot.rssi = -78;
    set_basic_id(s.pilot, 0, ODID_IDTYPE_SERIAL_NUMBER, "1581F000000000000000");
    set_basic_id(s.pilot, 1, ODID_IDTYPE_CAA_REGISTRATION_ID, "FA0EXAMPL1");
    set_location(s.pilot, 12.345678, -98.765432, 121, 275, 14);
    scenes.push_back(s);

    // Bluetooth, sitting still so no heading, southern/eastern hemisphere
    memset(&s, 0, sizeof(s));
    odid_initUasData(&s.pilot.uas_data);
    s.name = "ble_stationary";
    s.hasPilot = true;
    set_mac(s.pilot, 0x02);
    s.pilot.source = RID_SRC_BLE4;
    s.pilot.rssi = -8;
    s.pilot.uas_data.OperatorIDValid = 1; // Sent but empty, falls back to basic ID
    set_basic_id(s.pilot, 0, ODID_IDTYPE_CAA_REGISTRATION_ID, "FA0EXAMPL2");
    set_location(s.pilot, -1.234567, 123.456789, -1, INV_DIR, 0);
    scenes.push_back(s);

    // Longest operator ID, to check it against the source icon
    memset(&s, 0, sizeof(s));
    odid_initUasData(&s.pilot.uas_data);
    s.name = "ble_long_opid";
    s.hasPilot = true;
    set_mac(s.pilot, 0x03);
    s.pilot.source = RID_SRC_BLE4;
    s.pilot.rssi = -85;
    s.pilot.uas_data.OperatorIDValid = 1;
    strncpy(s.pilot.uas_data.OperatorID.OperatorId, "EXAMPLE-OPERATOR-ID1", ODID_ID_SIZE);
    set_location(s.pilot, 12.345678, -98.765432, 8888, 359, 254);
    scenes.push_back(s);

    // No IDs and every location value unknown, shows MAC and dashes
    memset(&s, 0, sizeof(s));
    odid_initUasData(&s.pilot.uas_data);
    s.name = "wifi_unknown";
    s.hasPilot = true;
    set_mac(s.pilot, 0x04);
    s.pilot.source = RID_SRC_WIFI_BEACON;
    s.pilot.rssi = -95;
    set_location(s.pilot, 0, 0, INV_ALT, INV_DIR, INV_SPEED_H);
    scenes.push_back(s);

    return scenes;
}

int main()
{
    TFT_eSPI tft;
    int failed = 0;
    mkdir(OUT_DIR, 0755);
    for (const Screen &scr : screens)
    {
        for (Scene &scene : make_scenes())
        {
            TFT_eSprite spr(&tft);
            spr.setColorDepth(16);
            spr.createSprite(scr.width, scr.height);

            // Same calls the firmware makes when a pilot is first shown
            display_init(&spr);
            if (scene.hasPilot)
            {
                display_clear_pilot();
                display_show_pilot(&scene.pilot, 123456);
                display_show_vbat(4012);
                display_show_active(true);
            }

            std::string path = std::string(OUT_DIR "/") + scr.name + "_" + scene.name + ".png";
            if (sprite_to_png(spr, path.c_str()))
                printf("%s\n", path.c_str());
            else
            {
                fprintf(stderr, "Failed to write %s\n", path.c_str());
                ++failed;
            }
            spr.deleteSprite();
        }
    }
    return failed ? 1 : 0;
}
