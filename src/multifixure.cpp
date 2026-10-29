#include "multifixure.h"
#include <FastLED.h>
#include <Arduino.h>

extern CRGB leds[];

enum MultiLook : uint8_t
{
    LookUni = 0,
    LookRainbow,
    LookSparkle,
    LookFlicker,
    LookWipe,
    LookBreathe,
    LookCrossing,
    LookChase
};

static MultiLook lookFromDmx(uint8_t v)
{
    if (v <= 31) return LookUni;
    if (v <= 63) return LookRainbow;
    if (v <= 95) return LookSparkle;
    if (v <= 127) return LookFlicker;
    if (v <= 159) return LookWipe;
    if (v <= 191) return LookBreathe;
    if (v <= 223) return LookCrossing;
    return LookChase;
}

static void tint(CRGB* dest, int n, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < n; i++)
    {
        dest[i].r = (uint8_t)((dest[i].r * r) / 255);
        dest[i].g = (uint8_t)((dest[i].g * g) / 255);
        dest[i].b = (uint8_t)((dest[i].b * b) / 255);
    }
}

static uint8_t rgbBri(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t m = r;
    if (g > m) m = g;
    if (b > m) m = b;
    return m;
}

static int variedLen(int baseLen, int key, uint8_t rnd)
{
    if (rnd == 0 || baseLen <= 1)
        return baseLen;
    int span = max(1, (baseLen * (int)rnd) / 255);
    uint8_t h = (uint8_t)(key * 47 + 19);
    int delta = (int)map(h, 0, 255, -span, span);
    return max(1, baseLen + delta);
}

static bool onTrainVaried(int pos, int head, int baseLen, int period, uint8_t rnd)
{
    int rel = pos - head;
    int cycle = rel / period;
    int within = rel % period;
    if (rel < 0)
    {
        cycle = (rel - (period - 1)) / period;
        within = rel - cycle * period;
    }
    int len = variedLen(baseLen, cycle, rnd);
    return within >= 0 && within < len;
}

bool renderMultifixture(const uint8_t* dmx512, int address1Based, int ledCount)
{
    if (!dmx512 || ledCount <= 0) return false;
    const int base = address1Based - 1;
    if (base < 0 || base + MULTIFIXTURE_SLOT > 512) return false;

    const uint8_t r = dmx512[base + 0];
    const uint8_t g = dmx512[base + 1];
    const uint8_t b = dmx512[base + 2];
    const MultiLook look = lookFromDmx(dmx512[base + 3]);
    const uint8_t speed = dmx512[base + 4];
    const uint8_t width = dmx512[base + 5];
    const uint8_t extra = dmx512[base + 6];
    const uint8_t rnd = dmx512[base + 7];

    FastLED.clear();

    switch (look)
    {
    case LookUni:
        fill_solid(leds, ledCount, CRGB(r, g, b));
        break;

    case LookRainbow:
    {
        static uint8_t hue;
        uint8_t delta = map(width, 0, 255, 1, 40);
        int step = map((int)speed, 0, 255, -6, 6);
        hue += (uint8_t)step;
        fill_rainbow(leds, ledCount, hue, delta);
        tint(leds, ledCount, r ? r : 255, g ? g : 255, b ? b : 255);
        break;
    }

    case LookSparkle:
    {
        int density = map(width, 0, 255, 1, max(1, ledCount / 2));
        uint8_t thresh = (uint8_t)map(speed, 0, 255, 4, 40);
        for (int i = 0; i < density; i++)
        {
            if (random8() < thresh + extra / 4)
                leds[random16(ledCount)] = CRGB(r, g, b);
        }
        fadeToBlackBy(leds, ledCount, 20);
        break;
    }

    case LookFlicker:
    {
        uint8_t duty = extra;
        bool on = random8() < (duty < 8 ? 40 : duty);
        uint8_t dim = on ? 255 : (uint8_t)map(width, 0, 255, 0, 80);
        CRGB c = CRGB((r * dim) / 255, (g * dim) / 255, (b * dim) / 255);
        if (on && random8() < map(speed, 0, 255, 10, 180))
            c = CRGB::White;
        fill_solid(leds, ledCount, c);
        break;
    }

    case LookWipe:
    {
        static uint16_t head;
        int w = max(1, (int)map((int)width, 0, 255, 1, ledCount));
        w = variedLen(w, (int)(head / max(1, ledCount)), rnd);
        int step = (int)map((int)speed, 0, 255, 1, 8);
        head = (uint16_t)((head + step) % max(1, (int)ledCount));
        for (int i = 0; i < w; i++)
            leds[(head + i) % ledCount] = CRGB(r, g, b);
        break;
    }

    case LookBreathe:
    {
        static uint8_t phase;
        phase += (uint8_t)map(speed, 0, 255, 1, 8);
        uint8_t wave = sin8(phase + extra + (uint8_t)(address1Based * 16));
        uint8_t lo = (uint8_t)map(width, 0, 255, 0, 180);
        uint8_t bri = lo + scale8(wave, 255 - lo);
        fill_solid(leds, ledCount, CRGB((r * bri) / 255, (g * bri) / 255, (b * bri) / 255));
        break;
    }

    case LookCrossing:
    {
        static uint16_t head;
        static uint8_t frac;
        int len = max(1, (int)map((int)width, 0, 255, 1, ledCount));
        int period = max(1, len + max(1, len / 2));
        frac += (uint8_t)map((int)speed, 0, 255, 1, 8);
        head = (uint16_t)(head + frac / 3);
        frac = (uint8_t)(frac % 3);
        CRGB c1 = CRGB(r, g, b);
        CRGB c2 = CRGB::White;
        if (extra != 0)
            c2 = CHSV(extra, 255, 255);
        c2.nscale8(rgbBri(r, g, b));
        for (int i = 0; i < ledCount; i++)
        {
            if (onTrainVaried(i, (int)head, len, period, rnd))
                leds[i] = c1;
            else if (onTrainVaried(ledCount - 1 - i, (int)head + period / 2, len, period, rnd))
                leds[i] = c2;
        }
        break;
    }

    case LookChase:
    {
        static uint16_t head;
        int len = max(1, (int)map((int)width, 0, 255, 1, ledCount));
        int gap = (int)map((int)extra, 0, 255, 0, ledCount);
        int period = max(1, len + gap);
        int step = (int)map((int)speed, 0, 255, 1, 8);
        head = (uint16_t)(head + step);
        CRGB c = CRGB(r, g, b);
        for (int i = 0; i < ledCount; i++)
        {
            if (onTrainVaried(i, (int)head, len, period, rnd))
                leds[i] = c;
        }
        break;
    }
    }

    return true;
}
