#pragma once

#include <stdint.h>

#define SPI_MODE0 0
#define MSBFIRST 1

class SPISettings
{
public:
    SPISettings() {}
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass
{
public:
    void begin() {}
    void end() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t) { return 0; }
    uint16_t transfer16(uint16_t) { return 0; }
    void setFrequency(uint32_t) {}
};

extern SPIClass SPI;
