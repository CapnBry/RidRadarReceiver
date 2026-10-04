// Minimal Arduino API so TFT_eSPI and the display code build on the host.
// Hardware calls do nothing, drawing goes to a TFT_eSprite in memory.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>

typedef bool boolean;
typedef uint8_t byte;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
// TFT_eSPI reads font table pointers with pgm_read_dword and keeps some in a
// uint32_t, so read the full pointer and link non-PIE (see platformio.ini) so
// the tables sit below 4GB
#define pgm_read_dword(addr) (*(const uintptr_t *)(addr))
#define F(s) (s)

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define DEG_TO_RAD 0.017453292519943295769236907684886

using std::min;
using std::max;
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return LOW; }
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline uint32_t millis() { return 0; }
inline void yield() {}
inline long random(long howsmall, long howbig) { return howsmall + rand() % (howbig - howsmall); }
inline long random(long howbig) { return random(0, howbig); }

inline char *ltoa(long value, char *str, int base)
{
    // Only used for base 10 by TFT_eSPI
    (void)base;
    sprintf(str, "%ld", value);
    return str;
}

class String
{
    std::string s;
public:
    String(const char *str = "") : s(str) {}
    unsigned int length() const { return s.length(); }
    const char *c_str() const { return s.c_str(); }
    bool operator==(const char *other) const { return s == other; }
    void toCharArray(char *buf, unsigned int bufsize) const
    {
        if (!buf || !bufsize)
            return;
        size_t n = std::min((size_t)bufsize - 1, s.length());
        memcpy(buf, s.c_str(), n);
        buf[n] = '\0';
    }
};

#include "Print.h"
