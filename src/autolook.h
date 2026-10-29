// Look autonome : celui que la barre joue quand il n'y a pas de live.
#ifndef POULPY_AUTOLOOK_H
#define POULPY_AUTOLOOK_H

#include <stdint.h>

void autoLookBegin(const uint8_t* look);
// Retourne faux si le look est déjà celui-là (répétitions et rediffusions ne coûtent rien).
bool autoLookSet(const uint8_t* look, uint16_t fadeMs);
const uint8_t* autoLookTarget();
void autoLookRender(unsigned long now, int ledCount, uint8_t seed);

#endif
