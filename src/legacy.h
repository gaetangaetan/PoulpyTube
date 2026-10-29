// Anciens modes (canal 1 = numéro de mode) : 0-12 et 255.
// Les modes 17-82 sont dans led_modes.cpp. Gardés tant que midiPoulpy les utilise.
#ifndef POULPY_LEGACY_H
#define POULPY_LEGACY_H

#include <stdint.h>

// Remplit leds[] sans l'envoyer. Retourne la période d'image en ms :
// ces modes avancent d'un pas par image, leur vitesse en dépend.
uint16_t renderLegacyMode(uint8_t mode);

#endif
