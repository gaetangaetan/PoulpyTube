// Nœud Art-Net de la passerelle : reçoit un univers sur son Wi-Fi (maison ou point d'accès)
// et le relaie aux barres en flux multifixture, comme PoulpyNode. Voir docs/ARCHITECTURE.md § 3.
#ifndef POULPY_ARTNET_H
#define POULPY_ARTNET_H

#include <Arduino.h>
#include <IPAddress.h>

struct ArtnetConfig {
  bool enabled;
  uint16_t universe;  // 0..32767, numéroté comme dans Pollux
  uint8_t blocks;     // blocs de 128 à relayer (bit b = bloc b) : ceux où une barre lit son slot
  IPAddress ip;       // adresse annoncée dans ArtPollReply
};

struct ArtnetStatus {
  bool listening;
  bool receiving;   // ArtDmx de l'univers reçu depuis moins de 2 s
  bool suspended;   // un autre émetteur (PoulpyNode, PoulpyLink, midiPoulpy) parle aux barres
  uint8_t fps;      // trames ArtDmx de l'univers par seconde
  IPAddress from;   // dernière console entendue
};

void artnetLoop(uint32_t now, const ArtnetConfig& c);
ArtnetStatus artnetStatus(uint32_t now);

#endif
