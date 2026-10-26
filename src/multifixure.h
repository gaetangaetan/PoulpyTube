#ifndef MULTIFIXTURE_H
#define MULTIFIXTURE_H

#include <stdint.h>

#define MULTIFIXTURE_SLOT 10
#define MULTIFIXTURE_TIMEOUT_MS 1000

bool renderMultifixture(const uint8_t* dmx512, int address1Based, int ledCount);

#endif
