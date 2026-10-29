#include "autolook.h"

#include <Arduino.h>
#include <string.h>

#include "multifixure.h"

static uint8_t fromLook[LOOK_SIZE];
static uint8_t toLook[LOOK_SIZE];
static unsigned long fadeStart = 0;
static uint16_t fadeMs = 0;

// Couleurs et réglages glissent ; le type de look (canal 4) et le déclencheur (canal 10)
// sautent au début du fondu, comme dans Pollux.
static void current(unsigned long now, uint8_t* out) {
  if (fadeMs == 0 || now - fadeStart >= fadeMs) {
    memcpy(out, toLook, LOOK_SIZE);
    return;
  }
  uint32_t t = (uint32_t)(now - fadeStart) * 256 / fadeMs;
  for (int i = 0; i < LOOK_SIZE; i++) {
    if (i == 3 || i == 9) {
      out[i] = toLook[i];
    } else {
      int d = (int)toLook[i] - (int)fromLook[i];
      out[i] = (uint8_t)(fromLook[i] + d * (int)t / 256);
    }
  }
}

void autoLookBegin(const uint8_t* look) {
  memcpy(fromLook, look, LOOK_SIZE);
  memcpy(toLook, look, LOOK_SIZE);
  fadeMs = 0;
}

bool autoLookSet(const uint8_t* look, uint16_t fade) {
  if (memcmp(look, toLook, LOOK_SIZE) == 0) return false;
  unsigned long now = millis();
  current(now, fromLook);
  memcpy(toLook, look, LOOK_SIZE);
  fadeStart = now;
  fadeMs = fade;
  return true;
}

const uint8_t* autoLookTarget() { return toLook; }

void autoLookRender(unsigned long now, int ledCount, uint8_t seed) {
  uint8_t look[LOOK_SIZE];
  current(now, look);
  renderLook(look, ledCount, seed);
}
