#ifndef LED_MODES_H
#define LED_MODES_H

#include <stdint.h>

// Modes étendus (17, 20-72, 80-82) — retourne true si le mode a été traité.
bool renderExtendedLedMode(uint8_t mode);

#endif
