// Réglages persistants du tube (EEPROM émulée, 256 octets).
//
//   0-1   adresse DMX (slot de 10 canaux, 1..503)
//   8     groupe (0..9)
//   12    OTA en attente : +0 drapeau, +1 long. SSID, +2 long. mot de passe,
//         +3 SSID (32), +35 mot de passe (63)
//   98    canal ESP-NOW (1..13)
//   99    0xA7 = adresse initialisée
//   100   nombre de LEDs (uint16)
//   102   URL OTA : longueur + 140 caractères
//   248+  énergie et limite de puissance (power_budget.cpp)
#ifndef POULPY_CONFIG_H
#define POULPY_CONFIG_H

#include <stdint.h>

#define MAXLEDLENGTH 300
#define DEFAULT_LED_COUNT 144

void configBegin();

uint8_t configLoadChannel();          // 0 si absent
void configSaveChannel(uint8_t channel);

uint16_t configLoadLedCount();
void configSaveLedCount(uint16_t count);

uint8_t configLoadGroup();
void configSaveGroup(uint8_t group);

// Adresse par défaut au premier démarrage : 1 + 10 × groupe.
int configLoadAddress(uint8_t group);
void configSaveAddress(int address);

void configSaveAll(uint8_t group, uint8_t channel, uint16_t ledCount);

void configSaveOta(const char* ssid, const char* password, const char* url);
bool configLoadOta(char* ssid, char* password, char* url);
void configClearOta();

#endif
