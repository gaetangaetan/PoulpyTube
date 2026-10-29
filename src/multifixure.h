#ifndef MULTIFIXTURE_H
#define MULTIFIXTURE_H

#include <stdint.h>

#define MULTIFIXTURE_SLOT 10
#define LOOK_SIZE MULTIFIXTURE_SLOT

// Rend un look de 10 octets (R, G, B, look, vitesse, largeur, extra, aléatoire,
// extra 2, déclencheur) dans leds[]. seed décale les animations d'une barre à l'autre.
// Un changement du canal 10 relance l'animation depuis le début.
void renderLook(const uint8_t* look, int ledCount, uint8_t seed);

// Slot de 10 canaux lu dans l'univers, à l'adresse 1-based.
bool renderMultifixture(const uint8_t* dmx512, int address1Based, int ledCount);

#endif
