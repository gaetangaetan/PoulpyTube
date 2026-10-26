#include "led_modes.h"

#include <Arduino.h>
#include <FastLED.h>
#include <math.h>

#ifndef NBGROUPS
#define NBGROUPS 10
#endif

extern CRGB leds[];
extern uint8_t dmxChannels[512];
extern uint16_t ledCount;
extern int setupTubeNumber;
extern uint8_t hueOffset;

namespace {

struct LMColor {
  uint8_t r, g, b;
};

static uint8_t s_lastMode = 0xFF;
static float s_phase = 0.0f;
static float s_phase2 = 0.0f;
static unsigned long s_lastMs = 0;
static uint8_t s_randGroup = 0;
static unsigned long s_randTick = 0;
static bool s_strobeOn = true;
static unsigned long s_strobeMs = 0;
static bool s_pixelStates[300];
static bool s_sparkInit = false;
static uint8_t s_ladderCount = 0;
static unsigned long s_ladderMs = 0;
static uint32_t s_binaryCounter = 0;
static unsigned long s_binaryMs = 0;
static int s_knightDir = 1;
static float s_knightPos = 0.0f;
static float s_pingHue = 0.0f;
static int s_snakeHead = 0;

static LMColor colorIdx(uint8_t index) {
  LMColor color{};
  if (index < 85) {
    color.r = index * 3;
    color.g = 255 - index * 3;
    color.b = 0;
  } else if (index < 170) {
    index -= 85;
    color.r = 255 - index * 3;
    color.g = 0;
    color.b = index * 3;
  } else {
    index -= 170;
    color.r = 0;
    color.g = index * 3;
    color.b = 255 - index * 3;
  }
  return color;
}

static CRGB crgbIdx(uint8_t idx, uint8_t bri = 255) {
  LMColor c = colorIdx(idx);
  return CRGB(scale8(c.r, bri), scale8(c.g, bri), scale8(c.b, bri));
}

static void fillSolid(CRGB c) {
  for (int i = 0; i < (int)ledCount; i++) {
    leds[i] = c;
  }
}

static int grpCh(int g, int off) {
  int ch = 1 + 3 * g + off;
  if (ch < 0 || ch >= 512)
    return 0;
  return dmxChannels[ch];
}

static float speedScale(uint8_t ch, float lo, float hi) {
  return map(ch, 0, 255, (long)(lo * 1000), (long)(hi * 1000)) / 1000.0f;
}

static int dmxMap(uint8_t ch, int inLo, int inHi, int outLo, int outHi) {
  return (int)map(dmxChannels[ch], inLo, inHi, outLo, outHi);
}

static int valMap(int v, int inLo, int inHi, int outLo, int outHi) {
  return (int)map(v, inLo, inHi, outLo, outHi);
}

static void onModeChange(uint8_t mode) {
  if (mode == s_lastMode)
    return;
  s_lastMode = mode;
  s_phase = 0.0f;
  s_phase2 = 0.0f;
  s_lastMs = millis();
  s_sparkInit = false;
  s_ladderCount = 0;
  s_ladderMs = millis();
  s_binaryCounter = 0;
  s_binaryMs = millis();
  s_knightDir = 1;
  s_knightPos = 0.0f;
  s_pingHue = 0.0f;
  s_snakeHead = 0;
  s_randGroup = random(0, NBGROUPS);
  s_randTick = millis();
}

static float tickDt() {
  unsigned long now = millis();
  float dt = (now - s_lastMs) / 1000.0f;
  s_lastMs = now;
  if (dt > 0.25f)
    dt = 0.25f;
  if (dt < 0.0f)
    dt = 0.0f;
  return dt;
}

static bool groupInRange(int g, int head, int width) {
  for (int w = 0; w < width; w++) {
    if (((head + w) % NBGROUPS + NBGROUPS) % NBGROUPS == g)
      return true;
  }
  return false;
}

static int ledsPerGroup(uint8_t lParam) {
  if (lParam == 0)
    return max(1, (int)ledCount);
  return max(1, (int)map(lParam, 1, 255, 1, 300));
}

static int virtualLen(int L) {
  return L * NBGROUPS;
}

static int localVirtualIndex(int localIdx, int L) {
  return setupTubeNumber * L + localIdx;
}

static void renderVirtualRange(float head, int segLen, CRGB color, int L, bool reverse, float fadeLen = 0.0f) {
  int vLen = virtualLen(L);
  if (vLen <= 0 || segLen < 1)
    return;

  while (head < 0)
    head += vLen;
  while (head >= vLen)
    head -= vLen;

  for (int i = 0; i < (int)ledCount; i++) {
    int vi = localVirtualIndex(i, L);
    float pos = reverse ? (float)(vLen - 1 - vi) : (float)vi;
    float rel = fmod(pos - head + (float)vLen, (float)vLen);
    if (rel < (float)segLen) {
      float bri = 1.0f;
      if (fadeLen > 0.5f) {
        if (rel < fadeLen)
          bri = rel / fadeLen;
        else if (rel > (float)segLen - fadeLen)
          bri = ((float)segLen - rel) / fadeLen;
      }
      leds[i].r = (uint8_t)(color.r * bri);
      leds[i].g = (uint8_t)(color.g * bri);
      leds[i].b = (uint8_t)(color.b * bri);
    }
  }
}

static void buildGroupOrder(uint8_t pattern, int out[NBGROUPS]) {
  for (int i = 0; i < NBGROUPS; i++)
    out[i] = i;

  if (pattern < 64)
    return;

  if (pattern < 128) {
    for (int i = 0; i < NBGROUPS; i++)
      out[i] = NBGROUPS - 1 - i;
    return;
  }

  if (pattern < 192) {
    int k = 0;
    for (int i = 0; i < NBGROUPS; i += 2)
      out[k++] = i;
    for (int i = 1; i < NBGROUPS; i += 2)
      out[k++] = i;
    return;
  }

  int k = 0;
  for (int i = 1; i < NBGROUPS; i += 2)
    out[k++] = i;
  for (int i = 0; i < NBGROUPS; i += 2)
    out[k++] = i;
}

// --- Mode 17 : blanc variable ---
static void mode17() {
  uint8_t warmth = dmxChannels[1];
  uint8_t bri = dmxChannels[2];
  uint8_t r = (uint8_t)((uint16_t)map(warmth, 0, 255, 160, 255) * bri / 255);
  uint8_t g = (uint8_t)((uint16_t)map(warmth, 0, 255, 190, 210) * bri / 255);
  uint8_t b = (uint8_t)((uint16_t)map(warmth, 0, 255, 255, 100) * bri / 255);
  fillSolid(CRGB(r, g, b));
}

// --- Modes 20-29 : animations locales ---
static void mode20() {
  uint8_t color = dmxChannels[1];
  float spd = speedScale(dmxChannels[2], 0.05f, 4.0f);
  int width = max(1, dmxMap(3, 0, 255, 1, (int)ledCount / 4 + 1));
  float dt = tickDt();
  s_phase += spd * dt * (float)ledCount;
  float head = fmod(s_phase, (float)max(1, (int)ledCount));
  CRGB c = crgbIdx(color);
  for (int i = 0; i < (int)ledCount; i++) {
    float d = fabsf((float)i - head);
    if (d > (float)ledCount / 2.0f)
      d = (float)ledCount - d;
    if (d <= (float)width / 2.0f) {
      float f = 1.0f - d / ((float)width / 2.0f + 0.001f);
      leds[i] = CRGB((uint8_t)(c.r * f), (uint8_t)(c.g * f), (uint8_t)(c.b * f));
    }
  }
}

static void mode21() {
  uint8_t color = dmxChannels[1];
  float spd = speedScale(dmxChannels[2], 0.05f, 3.0f);
  int segLen = max(1, dmxMap(3, 0, 255, 1, (int)ledCount));
  float dt = tickDt();
  s_phase += spd * dt * (float)(ledCount + segLen);
  int head = (int)fmod(s_phase, (float)(ledCount + segLen)) - segLen;
  CRGB c = crgbIdx(color);
  for (int i = 0; i < (int)ledCount; i++) {
    if (i >= head && i < head + segLen)
      leds[i] = c;
  }
}

static void mode22() {
  uint8_t color = dmxChannels[1];
  uint8_t rate = dmxChannels[2];
  uint8_t duty = dmxChannels[3];
  unsigned long period = map(rate, 0, 255, 2000, 50);
  unsigned long onTime = (unsigned long)period * duty / 255;
  unsigned long now = millis();
  if (now - s_strobeMs >= period) {
    s_strobeMs = now;
    s_strobeOn = true;
  } else if (s_strobeOn && (now - s_strobeMs >= onTime)) {
    s_strobeOn = false;
  }
  if (s_strobeOn)
    fillSolid(crgbIdx(color));
}

static void mode23() {
  uint8_t color = dmxChannels[1];
  float spd = speedScale(dmxChannels[2], 0.2f, 4.0f);
  uint8_t depth = dmxChannels[3];
  float dt = tickDt();
  s_phase += spd * dt;
  float s = (sinf(s_phase * 6.28318f) + 1.0f) * 0.5f;
  uint8_t bri = (uint8_t)(255 - ((255 - (255 * s)) * (255 - depth) / 255));
  fillSolid(crgbIdx(color, bri));
}

static void mode24() {
  uint8_t colorDensity = dmxChannels[1] / 2;
  uint8_t rainbowSpeed = dmxChannels[2];
  uint8_t deltaHue = map(colorDensity, 0, 255, 1, 50);
  fill_rainbow(leds, ledCount, (uint8_t)((hueOffset + setupTubeNumber * dmxChannels[3]) % 255), deltaHue);
  hueOffset = (uint8_t)(hueOffset + map(rainbowSpeed, 0, 255, -5, 5));
}

static void mode25() {
  if (!s_sparkInit) {
    for (int i = 0; i < 300; i++)
      s_pixelStates[i] = false;
    s_sparkInit = true;
  }
  uint8_t color = dmxChannels[1];
  int numPixels = max(1, dmxMap(2, 0, 255, 1, (int)ledCount / 2));
  unsigned long interval = map(dmxChannels[3], 0, 255, 500, 30);
  unsigned long now = millis();
  if (now - s_randTick > interval) {
    s_randTick = now;
    for (int i = 0; i < (int)ledCount; i++) {
      if (s_pixelStates[i] && random(0, 100) < 40)
        s_pixelStates[i] = false;
    }
    int active = 0;
    for (int i = 0; i < (int)ledCount; i++)
      if (s_pixelStates[i])
        active++;
    while (active < numPixels) {
      int p = random(0, (int)ledCount);
      if (!s_pixelStates[p]) {
        s_pixelStates[p] = true;
        active++;
      }
    }
  }
  CRGB c = crgbIdx(color);
  for (int i = 0; i < (int)ledCount; i++) {
    if (s_pixelStates[i])
      leds[i] = c;
  }
}

static void mode26() {
  fillSolid(CRGB(dmxChannels[1], dmxChannels[2], dmxChannels[3]));
}

static void mode27() {
  uint8_t color = dmxChannels[1];
  float spd = speedScale(dmxChannels[2], 0.05f, 2.0f);
  bool reverse = dmxChannels[3] > 127;
  float dt = tickDt();
  s_phase += spd * dt * (float)ledCount;
  int fillTo = (int)fmod(s_phase, (float)(ledCount + 1));
  CRGB c = crgbIdx(color);
  for (int i = 0; i < (int)ledCount; i++) {
    int idx = reverse ? ((int)ledCount - 1 - i) : i;
    if (idx < fillTo)
      leds[i] = c;
  }
}

static void mode28() {
  uint8_t color = dmxChannels[1];
  float baseSpd = speedScale(dmxChannels[2], 0.05f, 2.0f);
  float mult = map(grpCh(setupTubeNumber, 0), 0, 255, 25, 200) / 100.0f;
  float dt = tickDt();
  s_phase += baseSpd * mult * dt * (float)ledCount;
  int segLen = max(1, (int)ledCount / 8);
  int head = (int)fmod(s_phase, (float)(ledCount + segLen)) - segLen;
  CRGB c = crgbIdx(color);
  for (int i = 0; i < (int)ledCount; i++) {
    if (i >= head && i < head + segLen)
      leds[i] = c;
  }
}

static void mode29() {
  uint8_t color = dmxChannels[1];
  float spd = speedScale(dmxChannels[2], 0.05f, 2.0f);
  float phaseOff = grpCh(setupTubeNumber, 0) / 255.0f * (float)ledCount;
  float dt = tickDt();
  s_phase += spd * dt * (float)ledCount;
  int segLen = max(1, (int)ledCount / 6);
  float head = fmod(s_phase + phaseOff, (float)(ledCount + segLen)) - (float)segLen;
  CRGB c = crgbIdx(color);
  for (int i = 0; i < (int)ledCount; i++) {
    if ((float)i >= head && (float)i < head + (float)segLen)
      leds[i] = c;
  }
}

// --- Modes 30-49 : séquences inter-groupes ---
static void advanceSeqPhase(uint8_t speedCh, float scale = 1.0f) {
  float dt = tickDt();
  s_phase += speedScale(speedCh, 0.2f, 8.0f) * scale * dt;
}

static void mode30() {
  advanceSeqPhase(dmxChannels[2]);
  int head = ((int)s_phase) % NBGROUPS;
  int width = max(1, dmxMap(3, 0, 255, 1, NBGROUPS));
  if (groupInRange(setupTubeNumber, head, width))
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode31() {
  float dt = tickDt();
  s_phase += 2.0f * dt;
  int head = ((int)s_phase) % NBGROUPS;
  if (setupTubeNumber == head)
    fillSolid(CRGB(dmxChannels[1], dmxChannels[2], dmxChannels[3]));
}

static void mode32() {
  advanceSeqPhase(dmxChannels[2]);
  int head = ((int)s_phase) % NBGROUPS;
  CRGB c = crgbIdx(dmxChannels[1]);
  int dist = (setupTubeNumber - head + NBGROUPS) % NBGROUPS;
  if (dist == 0)
    fillSolid(c);
  else if (dist == 1)
    fillSolid(crgbIdx(dmxChannels[1], 128));
  else if (dist == 2)
    fillSolid(crgbIdx(dmxChannels[1], 64));
}

static void mode33() {
  advanceSeqPhase(dmxChannels[2]);
  int head = ((int)s_phase) % NBGROUPS;
  int trail = max(1, dmxMap(3, 0, 255, 1, NBGROUPS));
  int dist = (setupTubeNumber - head + NBGROUPS) % NBGROUPS;
  if (dist < trail) {
    uint8_t bri = (uint8_t)(255 - (255 * dist / trail));
    fillSolid(crgbIdx(dmxChannels[1], bri));
  }
}

static void mode34() {
  float spd = speedScale(dmxChannels[2], 0.2f, 3.0f);
  float dt = tickDt();
  s_knightPos += spd * dt * (float)s_knightDir;
  if (s_knightPos >= (float)(NBGROUPS - 1)) {
    s_knightPos = (float)(NBGROUPS - 1);
    s_knightDir = -1;
  } else if (s_knightPos <= 0.0f) {
    s_knightPos = 0.0f;
    s_knightDir = 1;
  }
  if (dmxChannels[3] > 127)
    s_knightPos = (float)(NBGROUPS - 1) - s_knightPos;
  int nearest = (int)(s_knightPos + 0.5f);
  if (setupTubeNumber == nearest)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode35() {
  advanceSeqPhase(dmxChannels[2], 0.5f);
  float wave = (sinf((setupTubeNumber + s_phase) * 0.8f) + 1.0f) * 0.5f;
  uint8_t bri = (uint8_t)(255 * wave);
  fillSolid(crgbIdx(dmxChannels[1], bri));
}

static void mode36() {
  advanceSeqPhase(dmxChannels[2]);
  int waveFront = ((int)s_phase) % (NBGROUPS * 2);
  int dist = abs(setupTubeNumber - (waveFront % NBGROUPS));
  if (waveFront >= NBGROUPS)
    dist = abs(setupTubeNumber - (NBGROUPS - 1 - (waveFront % NBGROUPS)));
  if (dist <= 1)
    fillSolid(crgbIdx(dmxChannels[1], dist == 0 ? 255 : 128));
}

static void mode37() {
  advanceSeqPhase(dmxChannels[2]);
  int origin = dmxMap(3, 0, 255, 0, NBGROUPS - 1);
  int waveFront = ((int)s_phase) % NBGROUPS;
  int dist = abs(setupTubeNumber - ((origin + waveFront) % NBGROUPS));
  if (dist <= 1)
    fillSolid(crgbIdx(dmxChannels[1], dist == 0 ? 255 : 100));
}

static void mode38() {
  unsigned long interval = map(dmxChannels[2], 0, 255, 2000, 100);
  unsigned long now = millis();
  if (now - s_randTick > interval) {
    s_randTick = now;
    s_randGroup = random(0, NBGROUPS);
  }
  if ((int)s_randGroup == setupTubeNumber)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode39() {
  float dt = tickDt();
  s_phase += speedScale(dmxChannels[3], 0.5f, 4.0f) * dt;
  bool showA = ((int)s_phase) % 2 == 0;
  bool even = (setupTubeNumber % 2) == 0;
  if (showA == even)
    fillSolid(crgbIdx(dmxChannels[1]));
  else
    fillSolid(crgbIdx(dmxChannels[2]));
}

static void mode40() {
  advanceSeqPhase(dmxChannels[2]);
  int step = max(1, dmxMap(3, 0, 255, 1, NBGROUPS));
  int head = ((int)s_phase) % step;
  if (setupTubeNumber % step == head)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode41() {
  unsigned long interval = map(dmxChannels[2], 0, 255, 2000, 200);
  unsigned long now = millis();
  if (now - s_ladderMs > interval) {
    s_ladderMs = now;
    if (s_ladderCount < NBGROUPS)
      s_ladderCount++;
  }
  if (setupTubeNumber < (int)s_ladderCount)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode42() {
  unsigned long interval = map(dmxChannels[2], 0, 255, 2000, 200);
  unsigned long now = millis();
  if (now - s_ladderMs > interval) {
    s_ladderMs = now;
    if (s_ladderCount < NBGROUPS)
      s_ladderCount++;
  }
  if (setupTubeNumber >= (int)s_ladderCount)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode43() {
  float dt = tickDt();
  s_phase += speedScale(dmxChannels[1], 0.1f, 2.0f) * dt;
  uint8_t hue = (uint8_t)(((setupTubeNumber * 25) + (int)(s_phase * 30)) % 256);
  fill_solid(leds, ledCount, CHSV(hue, 255, 255));
}

static void mode44() {
  float dt = tickDt();
  s_pingHue += speedScale(dmxChannels[2], 10.0f, 120.0f) * dt;
  int pos = ((int)(s_pingHue / 25.0f)) % NBGROUPS;
  if (setupTubeNumber == pos)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode45() {
  float bpm = map(dmxChannels[2], 0, 255, 30, 240);
  float dt = tickDt();
  s_phase += (bpm / 60.0f) * dt;
  int beat = (int)s_phase;
  if (setupTubeNumber == (beat % NBGROUPS))
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode46() {
  advanceSeqPhase(dmxChannels[2]);
  int gap = max(1, dmxMap(3, 0, 255, 1, NBGROUPS));
  int head = ((int)s_phase) % (NBGROUPS * gap);
  if (setupTubeNumber == (head / gap))
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode47() {
  unsigned long interval = map(dmxChannels[2], 0, 255, 2000, 150);
  unsigned long now = millis();
  if (now - s_binaryMs > interval) {
    s_binaryMs = now;
    s_binaryCounter++;
  }
  if ((s_binaryCounter >> setupTubeNumber) & 1)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode48() {
  advanceSeqPhase(dmxChannels[2], 0.3f);
  int active = ((int)s_phase) % NBGROUPS;
  int delayG = dmxMap(3, 0, 255, 0, NBGROUPS - 1);
  if (setupTubeNumber <= active && setupTubeNumber >= max(0, active - delayG))
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode49() {
  advanceSeqPhase(dmxChannels[2]);
  int len = 2 + (dmxChannels[3] > 127 ? 1 : 0);
  int head = ((int)s_phase) % NBGROUPS;
  bool rev = dmxChannels[3] <= 127;
  for (int k = 0; k < len; k++) {
    int g = rev ? ((head - k + NBGROUPS) % NBGROUPS) : ((head + k) % NBGROUPS);
    if (g == setupTubeNumber) {
      fillSolid(crgbIdx(dmxChannels[1]));
      break;
    }
  }
}

// --- Mode 53 : segment local par groupe ---
static void mode53() {
  int pos = valMap(grpCh(setupTubeNumber, 0), 0, 255, 0, max(0, (int)ledCount - 1));
  int len = max(1, valMap(grpCh(setupTubeNumber, 1), 0, 255, 1, (int)ledCount));
  CRGB c = crgbIdx(grpCh(setupTubeNumber, 2));
  for (int i = 0; i < (int)ledCount; i++) {
    if (i >= pos && i < pos + len)
      leds[i] = c;
  }
}

// --- Modes 60, 61, 63 : hybrides ---
static void mode60() {
  float spd = speedScale(dmxChannels[2], 0.2f, 6.0f);
  float off = grpCh(setupTubeNumber, 0) / 255.0f;
  float dt = tickDt();
  s_phase += spd * dt;
  int head = ((int)(s_phase + off * (float)NBGROUPS)) % NBGROUPS;
  if (setupTubeNumber == head)
    fillSolid(crgbIdx(dmxChannels[1]));
}

static void mode61() {
  float dt = tickDt();
  s_phase += speedScale(dmxChannels[1], 0.2f, 6.0f) * dt;
  int head = ((int)s_phase) % NBGROUPS;
  if (setupTubeNumber == head)
    fillSolid(CRGB(grpCh(setupTubeNumber, 0), grpCh(setupTubeNumber, 1), grpCh(setupTubeNumber, 2)));
}

static void mode63() {
  int order[NBGROUPS];
  buildGroupOrder(dmxChannels[3], order);
  advanceSeqPhase(dmxChannels[2]);
  int idx = ((int)s_phase) % NBGROUPS;
  if (order[idx] == setupTubeNumber)
    fillSolid(crgbIdx(dmxChannels[1]));
}

// --- Modes 64-72 : strip virtuelle cross-barres ---
static void mode64() {
  int L = ledsPerGroup(dmxChannels[4]);
  bool rev = dmxChannels[5] > 127;
  float spd = speedScale(dmxChannels[2], 0.05f, 3.0f);
  int segLen = max(1, dmxMap(3, 0, 255, 1, L));
  float dt = tickDt();
  s_phase += spd * dt * (float)virtualLen(L);
  renderVirtualRange(s_phase, segLen, crgbIdx(dmxChannels[1]), L, rev, 2.0f);
}

static void mode65() {
  int L = ledsPerGroup(dmxChannels[4]);
  bool rev = dmxChannels[5] > 127;
  float spd = speedScale(dmxChannels[2], 0.05f, 3.0f);
  int trail = max(1, dmxMap(3, 0, 255, 1, virtualLen(L) / 4));
  float dt = tickDt();
  s_phase += spd * dt * (float)virtualLen(L);
  for (int i = 0; i < (int)ledCount; i++) {
    int vi = localVirtualIndex(i, L);
    float pos = rev ? (float)(virtualLen(L) - 1 - vi) : (float)vi;
    float rel = fmod(s_phase - pos + (float)virtualLen(L) * 2.0f, (float)virtualLen(L));
    if (rel < (float)trail) {
      uint8_t bri = (uint8_t)(255 - (255 * (int)rel / trail));
      leds[i] = crgbIdx(dmxChannels[1], bri);
    }
  }
}

static void mode66() {
  int L = ledsPerGroup(dmxChannels[4]);
  bool rev = dmxChannels[5] > 127;
  float spd = speedScale(dmxChannels[2], 0.02f, 1.5f);
  float dt = tickDt();
  s_phase += spd * dt * (float)virtualLen(L);
  int fillTo = (int)fmod(s_phase, (float)(virtualLen(L) + 1));
  CRGB c = crgbIdx(dmxChannels[1]);
  for (int i = 0; i < (int)ledCount; i++) {
    int vi = localVirtualIndex(i, L);
    float pos = rev ? (float)(virtualLen(L) - 1 - vi) : (float)vi;
    if ((int)pos < fillTo)
      leds[i] = c;
  }
}

static void mode67() {
  int L = ledsPerGroup(dmxChannels[5]);
  float spd = speedScale(dmxChannels[3], 0.05f, 2.0f);
  int segLen = max(1, dmxMap(4, 0, 255, 1, L));
  float dt = tickDt();
  s_phase += spd * dt * (float)virtualLen(L);
  s_phase2 += spd * 0.7f * dt * (float)virtualLen(L);
  renderVirtualRange(s_phase, segLen, crgbIdx(dmxChannels[1]), L, false, 1.0f);
  renderVirtualRange(s_phase2, segLen, crgbIdx(dmxChannels[2]), L, true, 1.0f);
}

static void mode68() {
  int L = ledsPerGroup(dmxChannels[4]);
  float spd = speedScale(dmxChannels[2], 0.05f, 2.5f);
  int segLen = max(1, dmxMap(3, 0, 255, 1, L));
  int vLen = virtualLen(L);
  float dt = tickDt();
  s_phase += spd * dt * (float)vLen;
  float head = s_phase;
  if (head > (float)(vLen - 1))
    head = (float)(vLen - 1) - fmod(head - (float)(vLen - 1), (float)(vLen - 1));
  renderVirtualRange(head, segLen, crgbIdx(dmxChannels[1]), L, false, 2.0f);
}

static int vLenMin(int L) {
  return max(16, virtualLen(L) / 4);
}

static void mode69() {
  int L = ledsPerGroup(dmxChannels[3]);
  bool rev = dmxChannels[4] > 127;
  float spd = speedScale(dmxChannels[1], 0.05f, 2.0f);
  int band = max(8, dmxMap(2, 0, 255, 8, vLenMin(L)));
  float dt = tickDt();
  s_phase += spd * dt * (float)virtualLen(L);
  for (int i = 0; i < (int)ledCount; i++) {
    int vi = localVirtualIndex(i, L);
    float pos = rev ? (float)(virtualLen(L) - 1 - vi) : (float)vi;
    float rel = fmod(pos + s_phase, (float)band);
    uint8_t hue = (uint8_t)(rel / (float)band * 255.0f);
    leds[i] = CHSV(hue, 255, 255);
  }
}

static void mode70() {
  int L = ledsPerGroup(dmxChannels[4]);
  float spd = speedScale(dmxChannels[2], 0.05f, 2.0f);
  int step = max(2, dmxMap(3, 0, 255, 2, 8));
  float dt = tickDt();
  s_phase += spd * dt * (float)virtualLen(L);
  int head = (int)fmod(s_phase, (float)step);
  for (int i = 0; i < (int)ledCount; i++) {
    int vi = localVirtualIndex(i, L);
    if (vi % step == head)
      leds[i] = crgbIdx(dmxChannels[1]);
  }
}

static void mode71() {
  mode66(); // fill/wipe sur strip virtuelle
}

static void mode72() {
  int L = ledsPerGroup(dmxChannels[3]);
  float spd = speedScale(dmxChannels[2], 0.1f, 3.0f);
  int vLen = virtualLen(L);
  int origin = dmxMap(4, 0, 255, 0, max(0, vLen - 1));
  float dt = tickDt();
  s_phase += spd * dt * (float)NBGROUPS;
  int wave = ((int)s_phase) % NBGROUPS;
  for (int i = 0; i < (int)ledCount; i++) {
    int vi = localVirtualIndex(i, L);
    int dist = abs(vi - origin);
    if (dist == wave || dist == wave + 1)
      leds[i] = crgbIdx(dmxChannels[1], dist == wave ? 255 : 120);
  }
}

static int showActiveGroups(uint8_t ch) {
  int n = dmxChannels[ch];
  if (n < 1)
    n = NBGROUPS;
  if (n > NBGROUPS)
    n = NBGROUPS;
  return n;
}

static int showSegLen(uint8_t ch) {
  return max(1, (int)map(dmxChannels[ch], 0, 255, 1, max(1, (int)ledCount)));
}

static int showSegGap(uint8_t ch) {
  return max(0, (int)map(dmxChannels[ch], 0, 255, 0, (int)ledCount));
}

static bool showPixelOnTrain(int pos, int head, int len, int gap) {
  int period = max(1, len + gap);
  int rel = pos - head;
  rel %= period;
  if (rel < 0)
    rel += period;
  return rel < len;
}

static void mode80() {
  int N = showActiveGroups(11);
  if (setupTubeNumber < 0 || setupTubeNumber >= N)
    return;
  float dt = tickDt();
  s_phase += speedScale(dmxChannels[9], 0.2f, 4.0f) * dt;
  float off = map(dmxChannels[10], 0, 255, 0, 6283) / 1000.0f;
  uint8_t imin = dmxChannels[7];
  uint8_t imax = dmxChannels[8];
  uint8_t lo = imin < imax ? imin : imax;
  uint8_t hi = imin > imax ? imin : imax;
  int g = setupTubeNumber;
  float t = (N <= 1) ? 0.0f : (float)g / (float)(N - 1);
  uint8_t r = (uint8_t)((int)dmxChannels[1] + (int)(((int)dmxChannels[4] - (int)dmxChannels[1]) * t));
  uint8_t gg = (uint8_t)((int)dmxChannels[2] + (int)(((int)dmxChannels[5] - (int)dmxChannels[2]) * t));
  uint8_t b = (uint8_t)((int)dmxChannels[3] + (int)(((int)dmxChannels[6] - (int)dmxChannels[3]) * t));
  float s = (sinf(s_phase * 6.28318f + (float)g * off) + 1.0f) * 0.5f;
  float bri = (lo + s * (float)(hi - lo)) / 255.0f;
  fillSolid(CRGB((uint8_t)(r * bri), (uint8_t)(gg * bri), (uint8_t)(b * bri)));
}

static void mode81() {
  int N = showActiveGroups(15);
  if (setupTubeNumber < 0 || setupTubeNumber >= N)
    return;
  int count = max(1, (int)ledCount);
  int len1 = showSegLen(4);
  int gap1 = showSegGap(5);
  int len2 = showSegLen(11);
  int gap2 = showSegGap(12);
  int per1 = max(1, len1 + gap1);
  int per2 = max(1, len2 + gap2);
  float dt = tickDt();
  s_phase += speedScale(dmxChannels[6], 0.05f, 3.0f) * dt * (float)per1;
  s_phase2 += speedScale(dmxChannels[13], 0.05f, 3.0f) * dt * (float)per2;
  bool rev1 = dmxChannels[7] > 127;
  bool rev2 = dmxChannels[14] > 127;
  CRGB c1 = CRGB(dmxChannels[1], dmxChannels[2], dmxChannels[3]);
  CRGB c2 = CRGB(dmxChannels[8], dmxChannels[9], dmxChannels[10]);
  int head1 = (int)s_phase;
  int head2 = (int)s_phase2;
  for (int i = 0; i < count; i++) {
    int p1 = rev1 ? (count - 1 - i) : i;
    int p2 = rev2 ? (count - 1 - i) : i;
    if (showPixelOnTrain(p1, head1, len1, gap1))
      leds[i] = c1;
    else if (showPixelOnTrain(p2, head2, len2, gap2))
      leds[i] = c2;
  }
}

static void mode82() {
  int N = showActiveGroups(8);
  if (setupTubeNumber < 0 || setupTubeNumber >= N)
    return;
  int count = max(1, (int)ledCount);
  int len = showSegLen(4);
  int gap = showSegGap(5);
  int period = max(1, len + gap);
  float dt = tickDt();
  s_phase += speedScale(dmxChannels[6], 0.05f, 3.0f) * dt * (float)period;
  int gOff = (int)map(dmxChannels[7], 0, 255, 0, period);
  bool rev = dmxChannels[9] > 127;
  CRGB c = CRGB(dmxChannels[1], dmxChannels[2], dmxChannels[3]);
  int head = (int)s_phase;
  int shift = setupTubeNumber * gOff;
  for (int i = 0; i < count; i++) {
    int pos = rev ? (count - 1 - i) : i;
    if (showPixelOnTrain(pos, head + shift, len, gap))
      leds[i] = c;
  }
}

static bool isExtendedMode(uint8_t mode) {
  if (mode == 17)
    return true;
  if (mode >= 20 && mode <= 29)
    return true;
  if (mode >= 30 && mode <= 49)
    return true;
  if (mode == 53 || mode == 60 || mode == 61 || mode == 63)
    return true;
  if (mode >= 64 && mode <= 72)
    return true;
  if (mode >= 80 && mode <= 82)
    return true;
  return false;
}

} // namespace

bool renderExtendedLedMode(uint8_t mode) {
  if (!isExtendedMode(mode))
    return false;

  onModeChange(mode);

  switch (mode) {
  case 17: mode17(); break;
  case 20: mode20(); break;
  case 21: mode21(); break;
  case 22: mode22(); break;
  case 23: mode23(); break;
  case 24: mode24(); break;
  case 25: mode25(); break;
  case 26: mode26(); break;
  case 27: mode27(); break;
  case 28: mode28(); break;
  case 29: mode29(); break;
  case 30: mode30(); break;
  case 31: mode31(); break;
  case 32: mode32(); break;
  case 33: mode33(); break;
  case 34: mode34(); break;
  case 35: mode35(); break;
  case 36: mode36(); break;
  case 37: mode37(); break;
  case 38: mode38(); break;
  case 39: mode39(); break;
  case 40: mode40(); break;
  case 41: mode41(); break;
  case 42: mode42(); break;
  case 43: mode43(); break;
  case 44: mode44(); break;
  case 45: mode45(); break;
  case 46: mode46(); break;
  case 47: mode47(); break;
  case 48: mode48(); break;
  case 49: mode49(); break;
  case 53: mode53(); break;
  case 60: mode60(); break;
  case 61: mode61(); break;
  case 63: mode63(); break;
  case 64: mode64(); break;
  case 65: mode65(); break;
  case 66: mode66(); break;
  case 67: mode67(); break;
  case 68: mode68(); break;
  case 69: mode69(); break;
  case 70: mode70(); break;
  case 71: mode71(); break;
  case 72: mode72(); break;
  case 80: mode80(); break;
  case 81: mode81(); break;
  case 82: mode82(); break;
  default: break;
  }

  return true;
}
