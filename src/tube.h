// Ce que le mode autonome et la passerelle utilisent du tube (main.cpp).
#ifndef POULPY_TUBE_H
#define POULPY_TUBE_H

#include <stdint.h>

#include "config.h"
#include "protocol.h"

extern uint8_t myMac[6];
extern uint8_t detectedChannel;
extern int setupTubeNumber;  // groupe 0..9
extern int setupAddress;     // 0 = suit le groupe
extern uint16_t ledCount;
extern TubeSettings settings;

// Traite une commande comme si elle venait de la radio (la passerelle se parle à elle-même).
void tubeProcessPacket(const struct_dmx_packet& p);
// Diffuse un paquet ESP-NOW.
void tubeBroadcast(const struct_dmx_packet& p);
// Envoi vers une MAC (ajoute le pair si besoin). nullptr ou FF… = diffusion.
void tubeSendTo(const uint8_t* mac, const struct_dmx_packet& p);
// Univers reçu en Art-Net par la passerelle : appliqué comme un flux multifixture.
void tubeFeedLive(const uint8_t* dmx512);
// Dernier bloc live reçu par radio (millis | 1), 0 = jamais. La passerelle ne s'entend pas elle-même.
unsigned long tubeRadioLiveMs();
// Vrai tant que le flux live n'a pas dépassé le délai de retour.
bool tubeIsLive();
// Enregistre settings en EEPROM quelques secondes après le dernier changement.
void tubeSaveSettingsSoon();
void tubeRestart(unsigned long inMs);

#endif
