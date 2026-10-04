#include <Arduino.h>
#include "rid_frame_dumps.h"

// Write s as a quoted JSON string, escaping anything that isn't printable ASCII
static void serial_dump_string(const char *s)
{
    Serial.write('"');
    for (; *s; ++s)
    {
        uint8_t c = *s;
        if (c == '"' || c == '\\')
        {
            Serial.write('\\');
            Serial.write(c);
        }
        else if (c < ' ' || c > '~')
            Serial.printf("\\u%04x", c);
        else
            Serial.write(c);
    }
    Serial.write('"');
}

// Write "key":value, or "key":null when the value is unknown
static void serial_dump_float(const char *key, bool valid, double val, int decimals)
{
    if (valid)
        Serial.printf("\"%s\":%.*f", key, decimals, val);
    else
        Serial.printf("\"%s\":null", key);
}

static void serial_dump_int(const char *key, bool valid, int32_t val)
{
    if (valid)
        Serial.printf("\"%s\":%d", key, val);
    else
        Serial.printf("\"%s\":null", key);
}

void serial_dump_pilot(RadarPilotInfo *p)
{
    // JSON starts with "uasdata": <json version> and source MAC
    Serial.printf("{\"uasdata\":1,\"mac\":\"%02x%02x%02x%02x%02x%02x\"", MAC2STR(p->mac));
    Serial.printf(",\"src\":\"%s\",\"rssi\":%d", p->source == RID_SRC_BLE4 ? "ble4" : "wifi", p->rssi);

    serial_dump_basicids(p);
    serial_dump_opid(p);
    serial_dump_location(p);
    serial_dump_system(p);

    Serial.print("}\n");
}

void serial_dump_basicids(RadarPilotInfo *p)
{
    bool leadinSent = false;
    for (unsigned i=0; i<ODID_BASIC_ID_MAX_MESSAGES; ++i)
    {
        if (!p->uas_data.BasicIDValid[i] || p->uas_data.BasicID[i].IDType == ODID_IDTYPE_NONE)
            continue;

        if (!leadinSent)
        {
            leadinSent = true;
            Serial.print(",\"basicid\":[");
        }
        else
            Serial.write(',');

        Serial.printf("{\"type\":%d,\"uatype\":%d,\"id\":",
            p->uas_data.BasicID[i].IDType,
            p->uas_data.BasicID[i].UAType);
        serial_dump_string(p->uas_data.BasicID[i].UASID);
        Serial.write('}');
    }

    if (leadinSent)
        Serial.print("]");
}

void serial_dump_opid(RadarPilotInfo *p)
{
    if (!p->uas_data.OperatorIDValid)
        return;

    Serial.printf(",\"operatorid\":{\"type\":%d,\"id\":", p->uas_data.OperatorID.OperatorIdType);
    serial_dump_string(p->uas_data.OperatorID.OperatorId);
    Serial.write('}');
}

void serial_dump_location(RadarPilotInfo *p)
{
    if (!p->uas_data.LocationValid)
        return;

    const ODID_Location_data *loc = &p->uas_data.Location;
    bool posValid = rid_latlon_valid(loc->Latitude, loc->Longitude);
    Serial.print(",\"location\":{");
    serial_dump_float("lat", posValid, loc->Latitude, 6);
    Serial.write(',');
    serial_dump_float("lon", posValid, loc->Longitude, 6);
    Serial.write(',');
    serial_dump_int("hdg", rid_dir_valid(loc->Direction), (int32_t)loc->Direction);
    Serial.write(',');
    serial_dump_int("alt", rid_alt_valid(loc->Height), (int32_t)loc->Height);
    Serial.write(',');
    serial_dump_float("spdh", rid_speed_valid(loc->SpeedHorizontal), loc->SpeedHorizontal, 1);
    Serial.write('}');
}

void serial_dump_system(RadarPilotInfo *p)
{
    if (!p->uas_data.SystemValid)
        return;

    // ODID_operator_location_type_t OperatorLocationType;
    // ODID_classification_type_t ClassificationType;
    // double OperatorLatitude;  // Invalid, No Value, or Unknown: 0 deg (both Lat/Lon)
    // double OperatorLongitude; // Invalid, No Value, or Unknown: 0 deg (both Lat/Lon)
    // uint16_t AreaCount;       // Default 1
    // uint16_t AreaRadius;      // meter. Default 0
    // float AreaCeiling;        // meter. Invalid, No Value, or Unknown: -1000m
    // float AreaFloor;          // meter. Invalid, No Value, or Unknown: -1000m
    // ODID_category_EU_t CategoryEU; // Only filled if ClassificationType = ODID_CLASSIFICATION_TYPE_EU
    // ODID_class_EU_t ClassEU;       // Only filled if ClassificationType = ODID_CLASSIFICATION_TYPE_EU
    // float OperatorAltitudeGeo;// meter (WGS84-HAE). Invalid, No Value, or Unknown: -1000m
    // uint32_t Timestamp;       // Relative to 00:00:00 01/01/2019 UTC/Unix Time

    const ODID_System_data *sys = &p->uas_data.System;
    bool posValid = rid_latlon_valid(sys->OperatorLatitude, sys->OperatorLongitude);
    Serial.printf(",\"operator\":{\"loctype\":%d,", sys->OperatorLocationType);
    serial_dump_float("lat", posValid, sys->OperatorLatitude, 6);
    Serial.write(',');
    serial_dump_float("lon", posValid, sys->OperatorLongitude, 6);
    Serial.write(',');
    serial_dump_int("alt", rid_alt_valid(sys->OperatorAltitudeGeo), (int32_t)sys->OperatorAltitudeGeo);
    Serial.write('}');
}
