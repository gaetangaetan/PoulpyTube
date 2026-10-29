// Mode autonome : élection de la passerelle, balise, état des barres, et — sur la passerelle —
// Wi-Fi, page web, API, scènes. Voir docs/ARCHITECTURE.md § 5 à 7.
#ifndef POULPY_GATEWAY_H
#define POULPY_GATEWAY_H

#include <stdint.h>

// Après l'init ESP-NOW. Démarre la passerelle tout de suite si elle est désignée.
void gatewayBegin();
void gatewayLoop(unsigned long now);
bool gatewayActive();

// Commandes radio du mode autonome (appelées par main.cpp, hors réception).
void gatewayOnLook(const uint8_t* data);
void gatewayOnBeacon(const uint8_t* data);
void gatewayOnSetRole(const uint8_t* data);
void gatewayOnStatus(const uint8_t* data);

#endif
