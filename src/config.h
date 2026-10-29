// Réglages persistants du tube (EEPROM émulée, 512 octets).
//
// Disposition historique (inchangée, lue par les anciens firmwares) :
//   0-1   adresse DMX (slot de 10 canaux, 0 = suit le groupe, 1..503)
//   8     groupe (0..9)
//   12    OTA en attente : +0 drapeau, +1 long. SSID, +2 long. mot de passe,
//         +3 SSID (32), +35 mot de passe (63 ; au-delà de 51 il déborde sur 98+)
//   98    canal ESP-NOW (1..13)
//   99    0xA7 = adresse initialisée
//   100   nombre de LEDs (uint16)
//   102   URL OTA : longueur + 140 caractères
//   248+  énergie et limite de puissance (power_budget.cpp)
// Mode autonome (256+) : « PT », version, TubeSettings, CRC.
#ifndef POULPY_CONFIG_H
#define POULPY_CONFIG_H

#include <stdint.h>

#define MAXLEDLENGTH 300
#define DEFAULT_LED_COUNT 144
#define DEFAULT_RETURN_DELAY_S 4

struct TubeSettings {
  uint8_t role;          // 0 = barre, 1 = passerelle désignée
  uint8_t returnDelayS;  // délai avant de revenir au look autonome quand le live se tait
  uint8_t individual;    // 1 = look posé pour cette barre seule (les looks de groupe ne le remplacent pas)
  uint8_t autoLook[10];  // look autonome
  char wifiSsid[33];     // Wi-Fi de la maison (passerelle)
  char wifiPass[65];
};

void configBegin();

uint8_t configLoadChannel();          // 0 si absent
void configSaveChannel(uint8_t channel);

uint16_t configLoadLedCount();
void configSaveLedCount(uint16_t count);

uint8_t configLoadGroup();
void configSaveGroup(uint8_t group);

// Adresse par défaut au premier démarrage : 1 + 10 × groupe. 0 = suit le groupe.
int configLoadAddress(uint8_t group);
void configSaveAddress(int address);

void configSaveAll(uint8_t group, uint8_t channel, uint16_t ledCount);

void configSaveOta(const char* ssid, const char* password, const char* url);
bool configLoadOta(char* ssid, char* password, char* url);
void configClearOta();

// Valeurs par défaut si la zone est vide ou abîmée.
void configLoadSettings(TubeSettings& s);
void configSaveSettings(const TubeSettings& s);

#endif
