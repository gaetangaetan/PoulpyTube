#include "legacy.h"

#include <Arduino.h>
#include <FastLED.h>

#include "config.h"

extern CRGB leds[];
extern uint8_t dmxChannels[512];
extern uint16_t ledCount;
extern int setupTubeNumber;
extern uint8_t hueOffset;

#define FADE_SPEED 0.02

// Les modes 1 à 5 envoyaient le ruban deux fois par image : on garde leur cadence.
static const uint16_t FRAME_MS = 10;
static const uint16_t FRAME_MS_DOUBLE = 20;

struct Color {
  uint8_t r, g, b;
};

// Une couleur sur un seul canal DMX (rouge → vert → bleu).
static Color convertToColor(uint8_t index) {
  Color c;
  if (index < 85) {
    c = {(uint8_t)(index * 3), (uint8_t)(255 - index * 3), 0};
  } else if (index < 170) {
    index -= 85;
    c = {(uint8_t)(255 - index * 3), 0, (uint8_t)(index * 3)};
  } else {
    index -= 170;
    c = {0, (uint8_t)(index * 3), (uint8_t)(255 - index * 3)};
  }
  return c;
}

// ---------- Grésillement (modes 7 à 11) ----------
// Un cycle = phase allumée (le motif clignote) puis phase éteinte.
// L'affichage reste sur le dernier état pendant la phase éteinte.

static CRGB flickerLeds[MAXLEDLENGTH];

enum FlickerPhase : uint8_t { FLICKER_START, FLICKER_ON, FLICKER_OFF };

static struct {
  FlickerPhase phase = FLICKER_START;
  unsigned long phaseEnd = 0;
  unsigned long nextToggle = 0;
  unsigned long lastPattern = 0;
  unsigned long flickerMin = 0, flickerMax = 0, offMs = 0;
  bool isOn = false;
} flick;

// dmx2 longueur min, dmx3 longueur max, dmx4 durée allumée, dmx5 durée éteinte,
// dmx6 durée d'un clignotement, dmx7 changement de motif, dmx8-10 couleur.
static void flickering(uint8_t dmx2, uint8_t dmx3, uint8_t dmx4, uint8_t dmx5,
                       uint8_t dmx6, uint8_t dmx7, uint8_t r, uint8_t g, uint8_t b) {
  unsigned long now = millis();

  if (flick.phase == FLICKER_START) {
    int minLen = max(1, (dmx2 * (int)ledCount) / 255);
    int maxLen = max(1, (dmx3 * (int)ledCount) / 255);
    if (maxLen < minLen) {
      int t = maxLen;
      maxLen = minLen;
      minLen = t;
    }
    unsigned long patternMs = random(1, 100UL * dmx7);
    if (now - flick.lastPattern >= patternMs) {
      flick.lastPattern = now;
      bool lit = (random(2) == 1);
      for (int j = 0; j < ledCount;) {
        int segLen = random(minLen, maxLen);
        if (segLen < 1) segLen = 1;
        if (j + segLen > ledCount) segLen = ledCount - j;
        for (int k = 0; k < segLen; k++)
          flickerLeds[j + k] = lit ? CRGB(r, g, b) : CRGB::Black;
        lit = !lit;
        j += segLen;
      }
    }
    flick.flickerMin = dmx6 / 10;
    flick.flickerMax = dmx6;
    unsigned long offMax = random(5UL * dmx5, 10UL * dmx5);
    flick.offMs = offMax > 0 ? random(offMax / 2, offMax) : 0;
    flick.phaseEnd = now + random(5UL * dmx4, 10UL * dmx4);
    flick.nextToggle = now;
    flick.phase = FLICKER_ON;
  }

  if (flick.phase == FLICKER_ON) {
    if ((long)(now - flick.nextToggle) >= 0) {
      flick.isOn = !flick.isOn;
      flick.nextToggle = now + random(flick.flickerMin, flick.flickerMax + 1);
    }
    if ((long)(now - flick.phaseEnd) > 0) {
      flick.phase = FLICKER_OFF;
      flick.phaseEnd = now + flick.offMs;
    }
  } else if (flick.phase == FLICKER_OFF && (long)(now - flick.phaseEnd) >= 0) {
    flick.phase = FLICKER_START;
  }

  if (flick.isOn) {
    for (int i = 0; i < ledCount; i++) leds[i] = flickerLeds[i];
  }
}

// ---------- Scintillements (modes 3 à 5) ----------

static void randomPixels(bool fade) {
  uint8_t colorIndex = dmxChannels[1];
  uint8_t numPixels = dmxChannels[2] / 10;
  uint8_t rate = fade ? dmxChannels[3] / 2 : dmxChannels[3];

  static unsigned long lastUpdate = 0;
  static bool target[MAXLEDLENGTH];
  static float level[MAXLEDLENGTH];

  Color c = convertToColor(colorIndex);
  unsigned long interval = 1000 / map(rate, 0, 255, 1, 100);

  if (millis() - lastUpdate > interval) {
    lastUpdate = millis();
    int active = 0;
    for (int i = 0; i < ledCount; i++) {
      if (target[i] && random(0, 100) < 50) target[i] = false;
      if (target[i]) active++;
    }
    while (active < numPixels) {
      int p = random(0, ledCount);
      if (!target[p]) {
        target[p] = true;
        active++;
      }
    }
  }

  for (int i = 0; i < ledCount; i++) {
    float k;
    if (fade) {
      level[i] = target[i] ? min(level[i] + FADE_SPEED, 1.0) : max(level[i] - FADE_SPEED, 0.0);
      k = level[i];
    } else {
      k = target[i] ? 1.0f : 0.0f;
    }
    leds[i] = CRGB(c.r * k, c.g * k, c.b * k);
  }
}

static void randomBlocks() {
  uint8_t colorIndex = dmxChannels[1];
  uint8_t numGroups = dmxChannels[2] / 10;
  uint8_t rate = dmxChannels[3] / 2;

  static unsigned long lastUpdate = 0;
  static float level[MAXLEDLENGTH];
  static int target[MAXLEDLENGTH];
  static int length[MAXLEDLENGTH];

  Color c = convertToColor(colorIndex);
  unsigned long interval = 1000 / map(rate, 0, 255, 1, 100);

  if (millis() - lastUpdate > interval) {
    lastUpdate = millis();
    int active = 0;
    for (int i = 0; i < ledCount; i++) {
      if (target[i] == 1 && random(0, 100) < 50) target[i] = 0;
      if (target[i] == 1) active++;
    }
    while (active < numGroups) {
      int start = random(0, ledCount);
      int len = random(1, 6);
      if (target[start] == 0) {
        target[start] = 1;
        length[start] = len;
        active++;
      }
    }
  }

  for (int i = 0; i < ledCount; i++) {
    level[i] = target[i] == 1 ? min(level[i] + FADE_SPEED, 1.0) : max(level[i] - FADE_SPEED, 0.0);
    for (int j = 0; j < length[i]; j++) {
      int p = (i + j) % ledCount;
      leds[p] = CRGB(c.r * level[i], c.g * level[i], c.b * level[i]);
    }
  }
}

// ---------- Rendu ----------

uint16_t renderLegacyMode(uint8_t mode) {
  const int ir = 3 * setupTubeNumber + 1;
  const int ig = ir + 1;
  const int ib = ir + 2;

  if (mode != 1 && mode != 2 && mode != 24) hueOffset = 0;
  if (mode < 7 || mode > 11) flick.phase = FLICKER_START;

  switch (mode) {
    case 0:  // une couleur : canaux 2-4
      fill_solid(leds, ledCount, CRGB(dmxChannels[1], dmxChannels[2], dmxChannels[3]));
      return FRAME_MS;

    case 1:  // arc-en-ciel : ch2 décalage par groupe, ch3 densité, ch4 vitesse
    case 2: {  // idem + ch5 intensité
      uint8_t deltaHue = map(dmxChannels[2] / 2, 0, 255, 1, 50);
      fill_rainbow(leds, ledCount, (hueOffset + (setupTubeNumber * dmxChannels[1])) % 255, deltaHue);
      if (mode == 2)
        for (int i = 0; i < ledCount; i++) leds[i].fadeLightBy(255 - dmxChannels[4]);
      hueOffset += map(dmxChannels[3], 0, 255, -5, 5);
      return FRAME_MS_DOUBLE;
    }

    case 3:
      randomPixels(false);
      return FRAME_MS_DOUBLE;
    case 4:
      randomPixels(true);
      return FRAME_MS_DOUBLE;
    case 5:
      randomBlocks();
      return FRAME_MS_DOUBLE;

    case 6:  // RGB par groupe : groupe g = canaux 3g+2 .. 3g+4
      fill_solid(leds, ledCount, CRGB(dmxChannels[ir], dmxChannels[ig], dmxChannels[ib]));
      return FRAME_MS;

    case 7: {  // grésillement : ch2 ≠ 0 → couleur 9-11, sinon couleur du groupe
      int base = dmxChannels[1] != 0 ? 8 : 8 + 3 * setupTubeNumber;
      flickering(dmxChannels[2], dmxChannels[3], dmxChannels[4], dmxChannels[5],
                 dmxChannels[6], dmxChannels[7], dmxChannels[base], dmxChannels[base + 1],
                 dmxChannels[base + 2]);
      return FRAME_MS;
    }
    case 8:
      flickering(0, 183, 110, 93, 25, 61, 255, 68, 0);
      return FRAME_MS;
    case 9:
      flickering(0, 147, 29, 100, 63, 6, 0, 0, 255);
      return FRAME_MS;
    case 10:
      flickering(0, 147, 29, 100, 63, 6, 255, 255, 255);
      return FRAME_MS;
    case 11:
      flickering(0, 255, 255, 29, 31, 6, 255, 255, 255);
      return FRAME_MS;

    case 12: {  // segment : ch2-4 RGB, ch5 longueur, ch6 décalage
      int start = map((int)dmxChannels[5], 0, 255, 0, max(0, ledCount - 1));
      int segLen = map((int)dmxChannels[4], 0, 255, 1, ledCount);
      if (segLen < 1) segLen = 1;
      start = constrain(start, 0, ledCount - 1);
      if (start + segLen > ledCount) segLen = ledCount - start;
      for (int k = 0; k < segLen; k++)
        leds[start + k] = CRGB(dmxChannels[1], dmxChannels[2], dmxChannels[3]);
      return FRAME_MS;
    }

    case 255:  // numéro de groupe : une LED rouge toutes les 10
      for (int j = 0; j < setupTubeNumber && 10 * j < ledCount; j++) leds[10 * j] = CRGB::Red;
      return FRAME_MS;

    default:  // mode inconnu : noir
      return FRAME_MS;
  }
}
