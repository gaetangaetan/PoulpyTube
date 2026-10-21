#include "build_version.h"       // VERSION + VERSION_INT (scripts/update_version.py)
#define VERSION_TIMESTAMP __DATE__ " " __TIME__  // debug série uniquement
#define BUTTON_PRESENT false

// ========== SYSTÈME DE DÉBOGAGE ==========
#define DEBUG_ENABLED false // Démo : réduit le bruit série (remettre true pour debug)

// Macros pour le débogage (remplacent Serial.print/println)
#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif
// ========== FIN SYSTÈME DE DÉBOGAGE ==========
/*

Ce code fait partie d'un système de contrôle DMX sans fil pour ledstrip.

Le système est composé d'un émetteur qui dispose d'une entrée DMX (XLR 3 pins) et transmet les valeurs des 512 canaux à tous les récepteurs du système.

Le code qui suit sert à programmer les récepteurs.

Les contrôleurs utilisés sont 
- des ESP8266 (Wemos D1 mini) pour les récepteurs 
- et un ESP32 pour l'émetteur 

mais l'un comme l'autre fonctionnent sur les deux types de cartes, il faut juste adapter quelques lignes pour passer d'une carte à l'autre (essentiellement en ce qui concerne l'utilisation de ESP-NOW). 

Il n'y a pas de raison particulière à ce que l'émetteur utilise un ESP32 (je crois que c'est juste ce que j'avais sous la main quand je l'ai programmé)
L'émetteur comprend un bouton pour pouvoir régler le numéro de groupe (cf. modes de programmation) mais on peut très bien modifier quelques lignes pour que le numéro de groupe soit fixe

L'émetteur envoie 512 canaux DMX (par paquets de 128 canaux) et peut être décalé en modifiant la valeur de la variable "offsetDMXaddress" (qui vaut 0 par défaut)

Par exemple :
- si offsetDMXaddress vaut 0, la valeur à l'adresse 1 du récepteur DMX est envoyée à l'adresse 1 du récepteur
- si offsetDMXaddress vaut 240, la valeur à l'adresse 1 du récepteur DMX est envoyée à l'adresse 241 du récepteur

Si les adresses sont décalées, cela est totalement transparent du point de vue du récepteur. 
Quand on parle de la première adresse dans le code du récepteur, il s'agit de la première adresse des 512 valeurs qu'il reçoit, 
qu'importe le vrai canal DMX auquel cela correspond.

Quelques modes ont été programmés mais vu que j'utilise essentiellement le mode 6 (chaque groupe contrôlé sur trois canaux, en rgb), je n'ai pas passé un temps dingue là-dessus.
Pour plus de fonctionnalités, programmez vos propre modes dans la fonction DMX2LEDSTRIP

Fonctionnement :
Le canal DMX 1 précise le mode:
Mode = 0 : tous les pixels prennent la même valeur rgb représentée dans les canaux 2 3 4
Mode = 1 : rainbow | canal 2 = l'offset ajouté à chaque tube selon son groupe (0=tubes identiques) | canal 3 = rapprochement des couleurs | canal 4 = vitesse de défilement dans un sens ou dans l'autre (127=immobile)
Mode = 2 : idem avec un dimmer sur le canal 5 (le déplacement ne fonctionne plus -> à débugger)
Mode = 3 : random pixels | 3 canaux : couleur, nombre de pixels allumés, vitesse
Mode = 4 : idem avec fondu
Mode = 5 : idem par groupes de 1 à 5 pixels
Mode = 6 : rgb par grouope : chaque groupe utilise un 3 adresse pour une couleur RGB pour tous ses pixels (par exemple, le groupe 0 utilisera les adresses 2 3 4 pour ses valeurs RGB, le groupe 1 utilisera les adresses 5 6 7, ...)
// la numérotation des groupes commence à 0 (désolé, réflexe d'informaticien...)
Mode = 7 : "grésillement lumineux" : // 2 couleur unie ou par groupe 
// 3 longueur minimale 
// 4 longueur maximale 
// 5 durée d'une séquence de flickering 
// 6 durée d'une séquence éteinte 
// 7 durée d'un flicker individuel 
// 8 vitesse changement de pattern (pas encore utilisé ici) 
// 9 10 11 rgb grp0 | 12 13 14 rgb grp1 | etc.
Mode = 8, 9, 10, 11 : presets de flickering (pas de paramètres)
Mode = 12 : segment RGB local (ch2-4 RGB, ch5 longueur, ch6 décalage)
Mode = 17 : blanc variable (ch2 température, ch3 luminosité)
Modes 20-29 : animations locales (scanner, chase, strobe, breathe, rainbow, sparkle, wipe…)
Modes 30-49 : séquences inter-groupes (chase, comet, knight rider, wave, ripple…)
Mode = 53 : segment local par groupe (pos, len, couleur index via canaux groupe)
Modes 60, 61, 63 : hybrides séquence + paramètres par groupe
Modes 64-72 : strip virtuelle cross-barres (ch5 = leds/groupe, ch6 = sens)
Mode = 255 : affiche le numéro de groupe (des pixels espacés permettent de facilement voir le numéro du groupe, 7 pixels allumés= groupe 7)

remarque : le 04122024, j'ai réécrit tous les modes sauf les modes 0, 6 et 255 avec l'aide de chatgpt

SETUP (clic long pour y accéder ou en sortir) : réglage du numéro de groupe
Un certain nombre de LEDS correspondant au numéro de groupe clignotent en vert (si une seule led clignote en rouge, il s'agit du groupe 0 )
Le numéro de groupe est enregistré en EEPROM
*/


#if defined(ESP8266)
#define POULPY_LED_PIN D2  // pin de contrôle du strip led
#define BUTTONPIN D1       // on définit le pin positif du bouton (il s'agit d'un pullup, quand le bouton est relevé, la valeur du pin est HIGH, quand le bouton est enfoncé, le contact au GND est fait et la valeur est donc LOW)
#define BUTTONGROUNDPIN D5 // pour faciliter le montage, on utilise une pin pour fournir le GND au bouton
#else
#define POULPY_LED_PIN 16  // GPIO déjà câblé sur ce module (WLED l'utilisait)
#define BUTTONPIN 0
#define BUTTONGROUNDPIN 4
#endif

#define MAXLEDLENGTH 300  // Taille MAX des buffers (capacité maximale, ne pas changer!)
                          // Le nombre de LEDs effectivement utilisé est dans ledCount (chargé depuis EEPROM)
                          
                          // !!!  QUAND LE STRIP LED EST ALIMENTÉ PAR L'ESP (en cours de programmation, par exemple), NE PAS ALLUMER PLUS D'UNE DIZAINE DE LEDS !!!
                          
                          // en général, la longueur n'a pas particulièrement d'influence sur la latence du contrôleur mais ça peut être utile de la régler pour les programmes qui font le "tour" 
                          // du strip led (comme des segments de leds qui vont de bas en haut par exemple) 
                          // ou pour être sûr de ne pas demander plus de courant que ce que l'alimentation prévue ne peut fournir
                          

#define NBGROUPS 10 // nombre de groupes possibles (cf. modes de fonctionnement)

#define DEGRADE_FACTOR 0.5 // chatgpt - Proportion du segment utilisée pour le dégradé (0.5 = moitié du segment)
#define FADE_SPEED 0.02 // chatgpt -  Vitesse de transition : plus petit = transitions plus longues

// Signature pour valider les paquets Poulpylights
#define SIGNATURE_A 0x3C // 0011 1100
#define SIGNATURE_B 0x61 // 0110 0001  
#define SIGNATURE_C 0x05 // 0000 0101

// Codes de commande existants
#define OTA_UPDATE_CMD 0xFA // Code de commande pour lancer une mise à jour firmware
#define REQUEST_MAC_CMD 0xFB // Code de commande pour demander l'adresse MAC
#define RESPONSE_MAC_CMD 0xFC // Code de commande pour envoyer l'adresse MAC
#define ENTER_REMOTE_SETUP_CMD 0xFD // Code de commande pour entrer en setup à distance
#define EXIT_REMOTE_SETUP_CMD 0xFE // Code de commande pour sortir du setup à distance
#define SET_GROUP_NUMBER_CMD 0xFF // Code de commande pour définir le numéro de groupe
#define PANIC_RESTART_CMD 0xF9 // Code de commande pour redémarrer tous les récepteurs

// Déclenchement animation (ONIRIGUN / démo) : struct_dmx_packet complet, cmd en data[3], index en data[10]
#define ANIMATION_TRIGGER_CMD 0xA0

// ========== NOUVELLES COMMANDES (v67+) ==========
// Requêtes (émetteur -> récepteur)
#define GET_INFO_CMD           0xE0  // Demande toutes les infos du récepteur
#define GET_CHANNEL_CMD        0xE1  // Demande le canal WiFi actuel
#define START_SCAN_CMD         0xE2  // Lancer un scan des canaux
#define SET_CHANNEL_CMD        0xE3  // Définir le canal WiFi actif
#define SET_LED_COUNT_CMD      0xE4  // Définir le nombre de LEDs
#define START_STATS_CMD        0xE5  // Démarrer envoi stats périodiques
#define SET_DMX_ADDRESS_CMD    0xED  // Adresse DMX persistée (1–505)
#define SET_MULTIFIXTURE_CMD   0xEE  // RAM only : 0 = mono, 1 = slot 8 ch
#define GET_DMX_ADDRESS_CMD    0xEF  // Réponse 0xD7 : adresse EEPROM + flag runtime
#define STOP_STATS_CMD         0xE6  // Arrêter envoi stats périodiques
#define SET_BRIGHTNESS_CMD     0xE7  // Définir luminosité max
#define RESTART_SINGLE_CMD     0xE8  // Redémarrer ce récepteur spécifique
#define SAVE_CONFIG_CMD        0xE9  // Sauvegarder config en EEPROM
#define IDENTIFY_CMD           0xEA  // Faire clignoter pour identifier
#define PING_CMD               0xEB  // Test de connectivité
#define SET_STATS_SERIAL_CMD   0xEC  // Activer/désactiver stats sur serial
#define RESET_ENERGY_CMD       0xF0  // Remet à zéro l'énergie cumulée (EEPROM)
#define SET_POWER_LIMIT_CMD    0xF1  // Limite de puissance en watts, 0 = aucune (data[10..11] LE)

// Réponses (récepteur -> émetteur)
#define RESPONSE_INFO_CMD      0xD0  // Réponse avec toutes les infos
#define RESPONSE_CHANNEL_CMD   0xD1  // Réponse canal WiFi
#define RESPONSE_SCAN_CMD      0xD2  // Réponse résultats scan
#define RESPONSE_STATS_CMD     0xD3  // Stats périodiques
#define RESPONSE_ACK_CMD       0xD4  // Accusé de réception simple
#define RESPONSE_ERROR_CMD     0xD5  // Erreur
#define RESPONSE_PONG_CMD      0xD6  // Réponse au ping
#define RESPONSE_DMX_ADDRESS_CMD 0xD7  // Adresse + flag multifixure
// ========== FIN NOUVELLES COMMANDES ==========

#include <Arduino.h>
#include <EEPROM.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
extern "C" {
  #include "user_interface.h"
}
#define POULPY_HTTPUPDATE ESPhttpUpdate
#else
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "esp32_compat.h"
#define POULPY_HTTPUPDATE httpUpdate
#endif


// #include <DNSServer.h>
// #include <ESP8266WebServer.h>
// #include <WiFiManager.h> 
// WiFiManager wifiManager;
#define APNAME "mrLEDTUBE19"


#define EEPROM_SIZE 256  // Étendu pour stocker l'URL OTA

// Layout EEPROM :
//   0-3   : EEPROM_ADDR_SETUP_ADDRESS
//   4-7   : EEPROM_ADDR_SETUP_MODE
//   8     : EEPROM_ADDR_SETUP_TUBE
//   12    : EEPROM_ADDR_OTA_PARAMS
//             +0 pending, +1 ssidLen, +2 passLen
//             +3..+34 SSID (32 bytes), +35..+97 password (63 bytes)
//   98    : EEPROM_ADDR_DETECTED_CHANNEL
//   100   : EEPROM_ADDR_LED_COUNT (2 bytes)
//   102   : EEPROM_ADDR_OTA_URL — urlLength (1 byte) + url (max 140 chars)

#define EEPROM_ADDR_SETUP_ADDRESS    0
#define EEPROM_ADDR_SETUP_MODE       4
#define EEPROM_ADDR_SETUP_TUBE       8
#define EEPROM_ADDR_OTA_PARAMS       12
#define EEPROM_ADDR_DETECTED_CHANNEL 98
#define EEPROM_ADDR_MULTIFIX_INIT    99   // 0xA7 = adresse multifixure initialisée
#define EEPROM_ADDR_LED_COUNT        100  // uint16_t (2 bytes)
#define STREAM_KIND                  5
#define STREAM_MULTIFIXTURE          0x01
#define STREAM_MULTIFIXTURE_MARK     6
#define STREAM_MULTIFIXTURE_MAGIC    0xA5
#define MULTIFIX_INIT_MAGIC          0xA7
#define EEPROM_ADDR_OTA_URL          102  // urlLength (1 byte) + url (max 140 chars)

#include <FastLED.h>
#include "led_modes.h"
#include "power_budget.h"
#include "multifixure.h"

CRGB leds[MAXLEDLENGTH]; // objet représentant le ledstrip
CRGB flickerLeds[MAXLEDLENGTH]; // objet représentant la mémoire du ledstrip pendant l'effet flicker
uint8_t ledsTemp[MAXLEDLENGTH][3]; // tableau représentant les valeurs r g b de chaque led du ledstrip

#include "OneButton.h"
OneButton button1(BUTTONPIN, true); // Setup a new OneButton on pin BUTTONPIN.

//#include <ESP8266WiFiMulti.h>
#if defined(ESP8266)
#include <espnow.h>
#endif 
// library permettant d'utiliser le protocole de communication sans fil propriétaire d'Espressif (faire "comme du wifi" sans passer par toutes les couches du wifi)
// exemple : https://randomnerdtutorials.com/esp-now-esp32-arduino-ide/

int setupAddress = 1;
int setupMode = 1;
int setupTubeNumber = 1;
bool multifixtureEnabled = false;

double lastOffset = 0;

double lastOffsetR = 0;
double lastOffsetG = 0;
double lastOffsetB = 0;

double offsetInc = 0.1;

double offsetIncR = 0.1;
double offsetIncG = 0.1;
double offsetIncB = 0.1;

uint8_t tempr =0;
uint8_t tempg =0;
uint8_t tempb =0;

unsigned long lastFlicker = 0;
unsigned long debutAllume= 0;
unsigned long lastUpdateFlickerPattern = 0;

uint8_t hueOffset =0;
uint8_t randomHueOffset =0;

#define RUNNING true
#define SETUP false

bool etat = RUNNING; // etat peut être en mode SETUP ou RUNNING : pour entrer ou sortir du SETUP, il faut appuyer longuement sur le bouton, le ledstrip affiche alors le numéro n du groupe en allumant n leds vertes

// ========== VARIABLES POUR LE SETUP À DISTANCE ==========
bool remoteSetupMode = false; // Mode setup à distance activé
unsigned long lastBlinkTime = 0; // Pour le clignotement du numéro de groupe
bool blinkState = false; // État actuel du clignotement
                     // on peut changer de groupe en appuyant  sur le bouton, on incrémente le numéro de groupe jusque la limite (fixée à 10 pour le moment) au delà de laquelle on recommence au groupe 0
                     // le groupe 0 conrrespond à 1 led rouge pour le distinguer du groupe 1 (une led verte)

int flashInterval;

uint8_t dmxChannels[512]; // tableau dans lequel seront stockées les valeurs des 512 canaux DMX
                          // les éléments 0 à 511 représentent les valeurs DMX de 1 à 512. Éternelle bataille des gens qui commencent la numérotation à 0 et ceux qui commencent à 1. Perso, je trouve qu'on devrait toujours commencer à 0 mais bon... ;)

// Animations déclenchées par paquet Poulpylights (aligné ONIRIGUN / émetteur démo)
#define NB_DEMO_ANIMATIONS 10
static const uint8_t DEMO_ANIM_COLORS[NB_DEMO_ANIMATIONS][3] = {
    {255, 0, 50}, {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
    {0, 255, 255}, {200, 0, 255}, {255, 255, 255}, {128, 0, 255}, {255, 64, 0}};

enum DemoAnimPhase {
  DEMO_ANIM_IDLE,
  DEMO_ANIM_FLASH_HOLD,
  DEMO_ANIM_FADE_OUT,
  DEMO_ANIM_BLACK_HOLD,
  DEMO_ANIM_FADE_IN
};
volatile uint8_t demoAnimPendingIndex = 255;
static DemoAnimPhase demoAnimPhase = DEMO_ANIM_IDLE;
static CRGB demoAnimFlashColor;
static uint8_t demoRecoverR = 0;
static uint8_t demoRecoverG = 0;
static uint8_t demoRecoverB = 0;
static unsigned long demoAnimPhaseStartMs = 0;

#define DEMO_FLASH_HOLD_MS 80U
/** Fade ONIRIGUN : moitié de la durée précédente (~2,5 s → ~1,25 s) */
#define DEMO_FADE_OUT_MS 1250U
/** Noir complet avant retour à la couleur « normale » */
#define DEMO_BLACK_HOLD_MS 3500U
#define DEMO_FADE_IN_MS 1500U

typedef struct struct_dmx_packet // on divise les 512 adresses en 4 blocs de 128 adresses (on ne peut pas tout envoyer en une fois car la taille maximale des packets transmis par ESP-NOW est limitée à 250 bytes)
{                                
  uint8_t blockNumber;    // numéro du bloc (0..3)
  uint8_t dmxvalues[128]; // 128 valeurs DMX
  uint8_t data[100];      // données additionnelles (dont la signature en data[0..2])
} struct_dmx_packet;

struct_dmx_packet incomingDMXPacket;

// ========== VARIABLES POUR L'ENVOI DE PAQUETS ESP-NOW ==========
// Adresse broadcast pour envoyer des réponses
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

// Variable pour stocker l'adresse MAC de l'émetteur (pour répondre directement)
uint8_t senderMacAddress[6];
bool hasSenderAddress = false;

// Variables pour le mode configuration à distance
bool remoteConfigActive = false;      // true si en mode configuration à distance
unsigned long lastConfigBlink = 0;   // Dernier clignotement en mode config
bool configBlinkState = false;       // État du clignotement (true = allumé)

// ========== VARIABLES POUR STATISTIQUES ESP-NOW ==========
unsigned long lastStatsTime = 0;      // Dernière fois qu'on a affiché les stats
unsigned long statsDMXPackets = 0;   // Compteur de paquets DMX
unsigned long statsOTAPackets = 0;   // Compteur de paquets OTA
unsigned long statsMACPackets = 0;   // Compteur de paquets MAC (request/response)
unsigned long statsSETUPPackets = 0;  // Compteur de paquets SETUP
unsigned long statsInvalidPackets = 0; // Compteur de paquets invalides
unsigned long statsTotalPackets = 0; // Compteur total de paquets
int lastRSSI = 0;                    // RSSI (approximatif sur ESP8266 - limitation hardware)
// ========== FIN VARIABLES STATISTIQUES ==========

// ========== VARIABLES POUR SCAN CANAL WIFI ==========
uint8_t detectedChannel = 0;        // Canal détecté (0 = non détecté)
bool channelDetected = false;       // Flag de détection pendant le scan
uint8_t senderMacFromScan[6];      // MAC de l'émetteur détecté pendant le scan
unsigned long lastPacketTime = 0;   // Timestamp du dernier paquet reçu
static unsigned long lastMultifixureMs = 0;
bool isScanning = false;           // Flag pour éviter les scans multiples simultanés

// ========== VARIABLES POUR COMMANDES AVANCÉES ==========
bool statsStreamActive = false;          // Envoi périodique de stats activé
bool statsSerialEnabled = false;        // Affichage des stats sur le port série (désactivé par défaut)
unsigned long lastStatsStreamTime = 0;   // Dernier envoi de stats périodiques
uint32_t statsStreamPacketCount = 0;     // Paquets reçus depuis dernier envoi stats
uint8_t maxBrightness = 255;             // Luminosité maximale (0-255)
unsigned long bootTime = 0;              // Timestamp du démarrage (pour uptime)
bool identifyActive = false;             // Mode identification actif
unsigned long identifyEndTime = 0;       // Fin du mode identification
uint8_t scanResultsPerChannel[14] = {0}; // Résultats du scan (canaux 1-13, index 0 non utilisé)
uint8_t scanBestChannel = 0;             // Meilleur canal trouvé lors du scan
uint16_t ledCount = 144;                 // Nombre de LEDs effectivement utilisé (chargé depuis EEPROM au démarrage)
// ========== FIN VARIABLES COMMANDES AVANCÉES ==========
// ========== FIN VARIABLES SCAN CANAL WIFI ==========

void OnDataSent(u8 *mac_addr, u8 status) {} // quand on utilise ESP_NOW, la fonction OnDataSent doit être déclarée mais, concrètement, on n'en a pas besoin (pour l'instant, aucune donnée n'est renvoyée par les récepteurs à l'émetteur)
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len);
void OnScanReceive(uint8_t *mac, uint8_t *incomingData, uint8_t len); // Déclaration anticipée pour le callback de scan

// Fonction pour vérifier si une commande est destinée à ce récepteur
bool isCommandForMe(const uint8_t* commandData) {
  // Récupérer notre adresse MAC
  uint8_t myMacAddress[6];
  WiFi.macAddress(myMacAddress);
  
  // Comparer avec l'adresse MAC dans commandData[4..9]
  return memcmp(&commandData[4], myMacAddress, 6) == 0;
}

// Fonction pour afficher le pattern de configuration à distance
void displayRemoteConfigPattern() {
  unsigned long currentTime = millis();
  
  // Clignotement toutes les 500ms
  if (currentTime - lastConfigBlink >= 500) {
    configBlinkState = !configBlinkState;
    lastConfigBlink = currentTime;
    
    FastLED.clear();
    
    if (configBlinkState) {
      // État allumé : pattern rouge/blanc alternant toutes les 10 LEDs
      // LEDs 0-9 blancs, 10-19 rouges, 20-29 blancs, 30-39 rouges, etc.
      
      for (int i = 0; i < ledCount; i++) {
        int group = i / 10; // Groupe de 10 LEDs (0, 1, 2, 3...)
        if (group % 2 == 0) {
          leds[i] = CRGB::White; // Groupes pairs : blanc
        } else {
          leds[i] = CRGB::Red;   // Groupes impairs : rouge
        }
      }
    }
    // État éteint : rien à faire (FastLED.clear() déjà appelé)
    
    poulpyShow();
  }
}

// --- Couleur « normale » depuis le buffer DMX (mode 0 global ou mode 6 par groupe) ---
static void getNormalRgbFromDmx(uint8_t *r, uint8_t *g, uint8_t *b) {
  uint8_t mode = dmxChannels[0];
  if (mode == 0) {
    *r = dmxChannels[1];
    *g = dmxChannels[2];
    *b = dmxChannels[3];
    return;
  }
  if (mode == 6) {
    int ir = 3 * setupTubeNumber + 1;
    int ig = 3 * setupTubeNumber + 2;
    int ib = 3 * setupTubeNumber + 3;
    if (ir >= 0 && ir < 512 && ig < 512 && ib < 512) {
      *r = dmxChannels[ir];
      *g = dmxChannels[ig];
      *b = dmxChannels[ib];
    } else {
      *r = *g = *b = 0;
    }
    return;
  }
  if (mode == 12) {
    *r = dmxChannels[1];
    *g = dmxChannels[2];
    *b = dmxChannels[3];
    return;
  }
  *r = dmxChannels[1];
  *g = dmxChannels[2];
  *b = dmxChannels[3];
}

void armAnimationTrigger(uint8_t idx) {
  if (!etat)
    return;
  if (remoteSetupMode)
    return;
  if (remoteConfigActive)
    return;
  if (idx >= NB_DEMO_ANIMATIONS)
    idx = NB_DEMO_ANIMATIONS - 1;
  demoAnimPendingIndex = idx;
}

void updateDemoAnimation(void) {
  unsigned long now = millis();
  unsigned long dt = now - demoAnimPhaseStartMs;

  switch (demoAnimPhase) {
  case DEMO_ANIM_IDLE:
    return;

  case DEMO_ANIM_FLASH_HOLD:
    fill_solid(leds, ledCount, demoAnimFlashColor);
    poulpyShow();
    if (dt >= DEMO_FLASH_HOLD_MS) {
      demoAnimPhase = DEMO_ANIM_FADE_OUT;
      demoAnimPhaseStartMs = now;
    }
    break;

  case DEMO_ANIM_FADE_OUT: {
    float t = (float)dt / (float)DEMO_FADE_OUT_MS;
    if (t > 1.0f)
      t = 1.0f;
    uint8_t rr = (uint8_t)((float)demoAnimFlashColor.r * (1.0f - t));
    uint8_t gg = (uint8_t)((float)demoAnimFlashColor.g * (1.0f - t));
    uint8_t bb = (uint8_t)((float)demoAnimFlashColor.b * (1.0f - t));
    fill_solid(leds, ledCount, CRGB(rr, gg, bb));
    poulpyShow();
    if (t >= 1.0f) {
      demoAnimPhase = DEMO_ANIM_BLACK_HOLD;
      demoAnimPhaseStartMs = now;
    }
    break;
  }

  case DEMO_ANIM_BLACK_HOLD:
    fill_solid(leds, ledCount, CRGB::Black);
    poulpyShow();
    if (dt >= DEMO_BLACK_HOLD_MS) {
      getNormalRgbFromDmx(&demoRecoverR, &demoRecoverG, &demoRecoverB);
      demoAnimPhase = DEMO_ANIM_FADE_IN;
      demoAnimPhaseStartMs = now;
    }
    break;

  case DEMO_ANIM_FADE_IN: {
    float t = (float)dt / (float)DEMO_FADE_IN_MS;
    if (t > 1.0f)
      t = 1.0f;
    uint8_t rr = (uint8_t)((float)demoRecoverR * t);
    uint8_t gg = (uint8_t)((float)demoRecoverG * t);
    uint8_t bb = (uint8_t)((float)demoRecoverB * t);
    fill_solid(leds, ledCount, CRGB(rr, gg, bb));
    poulpyShow();
    if (t >= 1.0f)
      demoAnimPhase = DEMO_ANIM_IDLE;
    break;
  }

  default:
    demoAnimPhase = DEMO_ANIM_IDLE;
    break;
  }
}

// ========== FONCTIONNALITÉ OTA (MISE À JOUR FIRMWARE) ==========

// Fonctions utilitaires pour la gestion des paramètres OTA en EEPROM (approche simple)
void saveOTAParams(const char* ssid, const char* password, const char* firmwareUrl = nullptr) {
  uint8_t ssidLen = ssid ? strlen(ssid) : 0;
  uint8_t passLen = password ? strlen(password) : 0;
  if (ssidLen > 32) ssidLen = 32;
  if (passLen > 63) passLen = 63;

  EEPROM.write(EEPROM_ADDR_OTA_PARAMS,     1);        // pending = 1
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 1, ssidLen);
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 2, passLen);
  for (int i = 0; i < ssidLen; i++)
    EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 3 + i, ssid[i]);
  for (int i = 0; i < passLen; i++)
    EEPROM.write(EEPROM_ADDR_OTA_PARAMS + 35 + i, password[i]);

  // Sauvegarder l'URL dans la zone dédiée
  uint8_t urlLen = (firmwareUrl && strlen(firmwareUrl) > 0) ? strlen(firmwareUrl) : 0;
  if (urlLen > 140) urlLen = 140;
  EEPROM.write(EEPROM_ADDR_OTA_URL, urlLen);
  for (int i = 0; i < urlLen; i++)
    EEPROM.write(EEPROM_ADDR_OTA_URL + 1 + i, firmwareUrl[i]);

  EEPROM.commit();
  Serial.printf("Paramètres OTA sauvegardés (SSID=%s, URL=%s)\n",
                ssid ? ssid : "", firmwareUrl ? firmwareUrl : "(hardcodée)");
}

bool loadOTAParams(char* ssid, char* password, char* firmwareUrl = nullptr) {
  uint8_t pending = EEPROM.read(EEPROM_ADDR_OTA_PARAMS);
  if (pending != 1) return false;

  uint8_t ssidLen = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 1);
  uint8_t passLen = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 2);
  if (ssidLen > 32) ssidLen = 32;
  if (passLen > 63) passLen = 63;

  for (int i = 0; i < ssidLen; i++)
    ssid[i] = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 3 + i);
  ssid[ssidLen] = '\0';
  for (int i = 0; i < passLen; i++)
    password[i] = EEPROM.read(EEPROM_ADDR_OTA_PARAMS + 35 + i);
  password[passLen] = '\0';

  // Charger l'URL si le paramètre est fourni
  if (firmwareUrl) {
    uint8_t urlLen = EEPROM.read(EEPROM_ADDR_OTA_URL);
    if (urlLen > 0 && urlLen <= 140) {
      for (int i = 0; i < urlLen; i++)
        firmwareUrl[i] = EEPROM.read(EEPROM_ADDR_OTA_URL + 1 + i);
      firmwareUrl[urlLen] = '\0';
    } else {
      firmwareUrl[0] = '\0';  // Pas d'URL → utiliser la valeur par défaut
    }
  }
  return true;
}

void clearOTAParams() {
  EEPROM.write(EEPROM_ADDR_OTA_PARAMS, 0);  // pending = 0
  EEPROM.write(EEPROM_ADDR_OTA_URL, 0);     // effacer aussi la longueur de l'URL
  EEPROM.commit();
  Serial.println("Paramètres OTA effacés de l'EEPROM");
}

// ========== FONCTIONS GESTION EEPROM POUR CANAL DÉTECTÉ ==========
void saveDetectedChannel(uint8_t channel) {
  if (channel >= 1 && channel <= 13) {
    EEPROM.write(EEPROM_ADDR_DETECTED_CHANNEL, channel);
    EEPROM.commit();
    DEBUG_PRINTF("Canal %d sauvegardé en EEPROM\n", channel);
  } else {
    DEBUG_PRINTF("ERREUR: Tentative de sauvegarder un canal invalide: %d\n", channel);
  }
}

uint8_t loadDetectedChannel() {
  uint8_t channel = EEPROM.read(EEPROM_ADDR_DETECTED_CHANNEL);
  if (channel >= 1 && channel <= 13) {
    DEBUG_PRINTF("Canal chargé depuis EEPROM: %d\n", channel);
    return channel;
  } else {
    DEBUG_PRINTLN("Aucun canal valide en EEPROM");
    return 0;
  }
}

void clearDetectedChannel() {
  EEPROM.write(EEPROM_ADDR_DETECTED_CHANNEL, 0);
  EEPROM.commit();
  DEBUG_PRINTLN("Canal détecté effacé de l'EEPROM");
}
// ========== FIN FONCTIONS GESTION EEPROM CANAL ==========
// Programme une mise à jour OTA : extrait SSID/pass depuis data[] et
// l'URL optionnelle depuis dmxvalues[] (si dmxvalues != nullptr).
// Sauvegarde en EEPROM et redémarre.
void handleOTAUpdate(uint8_t* otaData, uint8_t* otaDmxValues) {
  Serial.println("========== PROGRAMMATION MISE À JOUR OTA ==========");

  uint8_t ssidLength = otaData[4];
  if (ssidLength == 0 || ssidLength > 32) {
    Serial.printf("ERREUR: Longueur SSID invalide (%d)\n", ssidLength);
    return;
  }

  char ssid[33];
  memcpy(ssid, &otaData[5], ssidLength);
  ssid[ssidLength] = '\0';

  uint8_t passwordLength = otaData[5 + ssidLength];
  if (passwordLength > 63) {
    Serial.printf("ERREUR: Longueur password invalide (%d)\n", passwordLength);
    return;
  }
  char password[64];
  if (passwordLength > 0)
    memcpy(password, &otaData[6 + ssidLength], passwordLength);
  password[passwordLength] = '\0';

  // URL optionnelle transmise dans le champ dmxvalues du paquet ESP-NOW
  char firmwareUrl[141] = {0};
  if (otaDmxValues && otaDmxValues[0] != 0) {
    strncpy(firmwareUrl, (char*)otaDmxValues, 140);
    firmwareUrl[140] = '\0';
    Serial.printf("URL OTA reçue: %s\n", firmwareUrl);
  }

  Serial.printf("OTA programmée avec WiFi: %s\n", ssid);
  saveOTAParams(ssid, password, firmwareUrl[0] ? firmwareUrl : nullptr);
  Serial.println("Redémarrage pour mise à jour OTA...");
  Serial.flush();
  delay(1000);
  ESP.restart();
}

// ========== FONCTION DE SCAN WIFI ==========
// Fonction pour scanner les réseaux WiFi et trouver le canal d'un SSID spécifique
int scanWiFiForChannel(const char* targetSSID) {
  DEBUG_PRINTLN("========== SCAN WIFI ==========");
  DEBUG_PRINTF("Recherche du réseau: %s\n", targetSSID);
  
  int n = WiFi.scanNetworks();
  DEBUG_PRINTF("Nombre de réseaux trouvés: %d\n", n);
  
  int foundChannel = -1;
  
  for (int i = 0; i < n; i++) {
    DEBUG_PRINTF("Canal %d: %s (RSSI: %d dBm, Chiffrement: %s)\n", 
                  WiFi.channel(i),
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "Ouvert" : "Protégé");
    
    if (WiFi.SSID(i) == targetSSID) {
      foundChannel = WiFi.channel(i);
      DEBUG_PRINTF(">>> RÉSEAU TROUVÉ sur canal %d !\n", foundChannel);
    }
  }
  
  DEBUG_PRINTLN("========== FIN SCAN WIFI ==========");
  return foundChannel;
}
// ========== FIN FONCTION DE SCAN WIFI ==========

// ========== FONCTION DE SCAN CANAL ESP-NOW ==========
// Fonction pour scanner les canaux 1-13 et détecter le canal utilisé par l'émetteur ESP-NOW
// Version améliorée : scanne TOUS les canaux et choisit celui avec le plus de paquets
// pour éviter les fuites radio isolées
uint8_t scanForChannel() {
  if (isScanning) {
    DEBUG_PRINTLN("Scan déjà en cours, ignoré");
    return 0;
  }
  
  isScanning = true;
  channelDetected = false;
  detectedChannel = 0;
  
  DEBUG_PRINTLN("========== SCAN CANAL ESP-NOW ==========");
  Serial.println("Recherche du canal ESP-NOW (scan complet)...");
  
  // Sauvegarder le canal actuel pour le restaurer en cas d'échec
  uint8_t originalChannel = wifi_get_channel();
  if (originalChannel == 0 || originalChannel > 13) {
    originalChannel = detectedChannel > 0 ? detectedChannel : 1;
  }
  
  // Tableau pour compter les paquets reçus sur chaque canal
  uint16_t packetCounts[14] = {0}; // Indices 0-13 (0 non utilisé)
  uint8_t bestChannel = 0;
  uint16_t maxPackets = 0;
  // Minimum de paquets pour valider un canal
  // Avec 200ms d'écoute et ~120 paquets/sec, le vrai canal devrait avoir ~24 paquets
  // Les canaux adjacents (fuites) en auront beaucoup moins
  const uint16_t MIN_PACKETS_TO_VALIDATE = 10;
  
  // Déinitialiser ESP-NOW avant de commencer le scan
  esp_now_deinit();
  delay(100);
  
  // Boucle sur les canaux 1-13 - TOUJOURS scanner tous les canaux
  for (uint8_t channel = 1; channel <= 13; channel++) {
    Serial.printf("Test canal %d...\n", channel);
    
    // Indication visuelle : allumer progressivement les LEDs bleues
    FastLED.clear();
    int numLedsToShow = (channel <= ledCount) ? channel : ledCount;
    for (int i = 0; i < numLedsToShow; i++) {
      leds[i] = CRGB::Blue;
    }
    poulpyShow();
    
    // Configurer le canal WiFi AVANT d'initialiser ESP-NOW
    wifi_set_channel(channel);
    delay(200);
    
    // Réinitialiser ESP-NOW sur le nouveau canal
    esp_now_deinit();
    delay(100);
    
    int initResult = esp_now_init();
    if (initResult != 0) {
      Serial.printf("ERREUR: Échec init ESP-NOW sur canal %d (code: %d)\n", channel, initResult);
      continue;
    }
    
    // Configurer ESP-NOW pour la réception
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    
    // Enregistrer le callback de scan temporaire
    esp_now_register_recv_cb(OnScanReceive);
    
    // Ajouter le peer broadcast sur ce canal
    int peerResult = esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, channel, NULL, 0);
    if (peerResult != 0) {
      Serial.printf("ERREUR: Échec ajout peer broadcast sur canal %d (code: %d)\n", channel, peerResult);
      esp_now_deinit();
      delay(50);
      continue;
    }
    
    // Réinitialiser le compteur pour ce canal
    channelDetected = false;
    uint16_t packetsOnThisChannel = 0; // uint16_t pour éviter débordement
    
    // Écouter pendant 200ms (suffisant pour ~24 paquets à 120/sec)
    // Réduit pour éviter les WDT reset sur canaux très actifs
    unsigned long scanStartTime = millis();
    while (millis() - scanStartTime < 200) {
      yield(); // IMPORTANT: laisser le système respirer
      
      // Si un paquet est détecté, incrémenter le compteur
      if (channelDetected) {
        packetsOnThisChannel++;
        channelDetected = false; // Réinitialiser pour détecter le prochain paquet
        // Pas de log ici pour éviter WDT reset !
      }
    }
    
    // Enregistrer le nombre de paquets reçus sur ce canal (plafonné à 255)
    packetCounts[channel] = (packetsOnThisChannel > 255) ? 255 : packetsOnThisChannel;
    Serial.printf("Canal %d: %d paquets\n", channel, packetsOnThisChannel);
    
    // Mettre à jour le meilleur canal si nécessaire
    if (packetsOnThisChannel > maxPackets) {
      maxPackets = packetsOnThisChannel;
      bestChannel = channel;
    }
    
    // Déinitialiser ESP-NOW avant de passer au canal suivant
    esp_now_deinit();
    delay(50);
  }
  
  // Analyser les résultats
  DEBUG_PRINTLN("========== RÉSULTATS DU SCAN ==========");
  for (uint8_t ch = 1; ch <= 13; ch++) {
    if (packetCounts[ch] > 0) {
      Serial.printf("  Canal %d: %d paquets\n", ch, packetCounts[ch]);
    }
  }
  
  // Vérifier si on a trouvé un canal valide (minimum MIN_PACKETS_TO_VALIDATE paquets)
  if (maxPackets >= MIN_PACKETS_TO_VALIDATE) {
    detectedChannel = bestChannel;
    Serial.printf(">>> CANAL VALIDÉ : %d (%d paquets reçus) !\n", detectedChannel, maxPackets);
    
    // Flash vert rapide (3 clignotements)
    for (int i = 0; i < 3; i++) {
      FastLED.clear();
      for (int j = 0; j < min(10, (int)ledCount); j++) {
        leds[j] = CRGB::Green;
      }
      poulpyShow();
      delay(100);
      FastLED.clear();
      poulpyShow();
      delay(100);
    }
    
    // Sauvegarder le canal en EEPROM
    saveDetectedChannel(detectedChannel);
    
    // Configurer le canal WiFi final
    wifi_set_channel(detectedChannel);
    delay(200);
    
    // Réinitialiser ESP-NOW sur le canal détecté
    esp_now_deinit();
    delay(100);
    if (esp_now_init() != 0) {
      DEBUG_PRINTLN("ERREUR: Échec réinit ESP-NOW après scan réussi");
      isScanning = false;
      return 0;
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);
    esp_now_register_recv_cb(OnDataRecv);
    
    isScanning = false;
    DEBUG_PRINTLN("========== FIN SCAN CANAL ESP-NOW (SUCCÈS) ==========");
    return detectedChannel;
  } else {
    // Aucun canal valide trouvé
    Serial.println("Aucun canal ESP-NOW valide détecté (pas assez de paquets)");
    Serial.printf("Meilleur canal: %d (%d paquets, minimum requis: %d)\n", 
                 bestChannel, maxPackets, MIN_PACKETS_TO_VALIDATE);
    
    // Restaurer le canal WiFi original
    wifi_set_channel(originalChannel);
    delay(200);
    
    // Réinitialiser ESP-NOW sur le canal original
    if (esp_now_init() != 0) {
      DEBUG_PRINTLN("ERREUR: Échec réinit ESP-NOW après scan (canal original)");
      isScanning = false;
      return 0;
    }
    
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, originalChannel, NULL, 0);
    esp_now_register_recv_cb(OnDataRecv);
    
    isScanning = false;
    DEBUG_PRINTLN("========== FIN SCAN CANAL ESP-NOW (ÉCHEC) ==========");
    return 0;
  }
}
// ========== FIN FONCTION DE SCAN CANAL ESP-NOW ==========

// Fonction pour exécuter la mise à jour OTA au démarrage
void executeOTAUpdate() {
  //Serial.println("DEBUG: executeOTAUpdate() appelée");
  
  char ssid[33];
  char password[64];
  char savedUrl[141];

  if (!loadOTAParams(ssid, password, savedUrl)) {
    return;
  }

  Serial.println("========== EXÉCUTION MISE À JOUR OTA ==========");
  Serial.printf("Connexion au WiFi: %s\n", ssid);

  clearOTAParams();

  // URL versionnée reçue de l'émetteur, ou URL legacy hardcodée en fallback
  const char *firmwareURL = (savedUrl[0] != '\0')
    ? savedUrl
    : "http://poulpylights.gaetanstreel.com/receiver/firmware.bin";
  Serial.printf("URL firmware: %s\n", firmwareURL);
  
  // Clignotement bleu pendant la connexion WiFi
  FastLED.clear();
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < min(10, MAXLEDLENGTH); j++) {
      leds[j] = CRGB::Blue;
    }
    poulpyShow();
    delay(200);
    FastLED.clear();
    poulpyShow();
    delay(200);
  }
  
  // Connexion au WiFi
  WiFi.mode(WIFI_STA);
  
  // Scanner pour trouver le canal du réseau
  DEBUG_PRINTLN("Scan des réseaux WiFi pour trouver le canal...");
  int wifiChannel = scanWiFiForChannel(ssid);
  if (wifiChannel > 0) {
    DEBUG_PRINTF("Configuration du canal WiFi à %d\n", wifiChannel);
    wifi_set_channel(wifiChannel);
  } else {
    DEBUG_PRINTLN("ATTENTION: Canal non trouvé lors du scan, utilisation du canal par défaut");
  }
  
  DEBUG_PRINTF("Canal WiFi actuel avant connexion: %d\n", wifi_get_channel());
  WiFi.begin(ssid, password);
  
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 30) { // 30 secondes maximum
    delay(1000);
    wifiTimeout++;
    Serial.print(".");
    DEBUG_PRINTF(" (tentative %d/30, canal: %d)\n", wifiTimeout, wifi_get_channel());
    
    // Indication visuelle : une LED bleue qui progresse
    FastLED.clear();
    for (int j = 0; j < min(wifiTimeout, (int)ledCount); j++) {
      leds[j] = CRGB::Blue;
    }
    poulpyShow();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nERREUR: Connexion WiFi échouée");
    DEBUG_PRINTF("Canal utilisé lors de l'échec: %d\n", wifi_get_channel());
    // Indication visuelle : clignotement rouge
    for (int i = 0; i < 10; i++) {
      FastLED.clear();
      for (int j = 0; j < min(5, (int)ledCount); j++) {
        leds[j] = CRGB::Red;
      }
      poulpyShow();
      delay(100);
      FastLED.clear();
      poulpyShow();
      delay(100);
    }
    
    // Redémarrage en mode normal (paramètres déjà effacés)
    Serial.println("Redémarrage en mode normal...");
    delay(1000);
    ESP.restart();
    return;
  }
  
  Serial.println("\nWiFi connecté !");
  DEBUG_PRINTF("Canal WiFi utilisé: %d\n", wifi_get_channel());
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());
  DEBUG_PRINTF("RSSI: %d dBm\n", WiFi.RSSI());
  
  // Indication visuelle : vert fixe pendant le téléchargement
  FastLED.clear();
  for (int j = 0; j < ledCount; j++) {
    leds[j] = CRGB::Green;
  }
  poulpyShow();
  
  WiFiClient client;
  client.setTimeout(60000);

#if defined(ESP8266)
  POULPY_HTTPUPDATE.setClientTimeout(60000);
#endif
  t_httpUpdate_return result = POULPY_HTTPUPDATE.update(client, firmwareURL);
  
  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("ERREUR: Mise à jour échouée (%d): %s\n", 
                    POULPY_HTTPUPDATE.getLastError(), 
                    POULPY_HTTPUPDATE.getLastErrorString().c_str());
      
      // Indication visuelle : clignotement rouge rapide
      for (int i = 0; i < 20; i++) {
        FastLED.clear();
        for (int j = 0; j < ledCount; j++) {
          leds[j] = CRGB::Red;
        }
        poulpyShow();
        delay(50);
        FastLED.clear();
        poulpyShow();
        delay(50);
      }
      break;
      
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("Aucune mise à jour disponible");
      
      // Indication visuelle : orange fixe 2 secondes
      FastLED.clear();
      for (int j = 0; j < ledCount; j++) {
        leds[j] = CRGB::Orange;
      }
      poulpyShow();
      delay(2000);
      break;
      
    case HTTP_UPDATE_OK:
      Serial.println("Mise à jour réussie ! Redémarrage...");
      
      // Indication visuelle : blanc fixe 1 seconde puis redémarrage
      FastLED.clear();
      for (int j = 0; j < ledCount; j++) {
        leds[j] = CRGB::White;
      }
      poulpyShow();
      delay(1000);
      
      // Paramètres déjà effacés, redémarrage automatique par OTA
      ESP.restart();
      break;
  }
  
  // Si on arrive ici, c'est qu'il y a eu une erreur ou aucune mise à jour
  // Paramètres déjà effacés, redémarrage en mode normal
  Serial.println("Redémarrage en mode normal...");
  delay(1000);
  ESP.restart();
  
  Serial.println("========== FIN MISE À JOUR OTA ==========");
}
// ========== FIN FONCTIONNALITÉ OTA ==========

// ========== FONCTIONNALITÉ DÉCOUVERTE MAC ==========
// Fonction encapsulée pour envoyer l'adresse MAC de ce récepteur
// En réponse à une demande REQUEST_MAC_CMD
void sendMACResponse() {
  Serial.println("========== ENVOI ADRESSE MAC ==========");
  
  // Préparation du paquet de réponse
  struct_dmx_packet responsePacket;
  
  // Numéro de bloc = 255 pour indiquer que c'est un paquet de commande (pas de données DMX)
  responsePacket.blockNumber = 255;
  
  // Effacer les données DMX (non utilisées pour ce type de paquet)
  memset(responsePacket.dmxvalues, 0, sizeof(responsePacket.dmxvalues));
  
  // Effacer les données additionnelles
  memset(responsePacket.data, 0, sizeof(responsePacket.data));
  
  // Signature pour valider le paquet
  responsePacket.data[0] = SIGNATURE_A;
  responsePacket.data[1] = SIGNATURE_B;
  responsePacket.data[2] = SIGNATURE_C;
  
  // Code de commande de réponse MAC
  responsePacket.data[3] = RESPONSE_MAC_CMD;
  
  // Récupération de l'adresse MAC de ce ESP8266
  uint8_t macAddress[6];
  WiFi.macAddress(macAddress);
  
  // Insertion de l'adresse MAC dans le paquet (6 bytes)
  for (int i = 0; i < 6; i++) {
    responsePacket.data[4 + i] = macAddress[i];
  }
  
  // Informations supplémentaires utiles pour l'émetteur
  responsePacket.data[10] = setupTubeNumber; // Numéro de groupe actuel
  responsePacket.data[11] = VERSION_INT;     // Version du firmware (entier 1-byte, legacy)
  
  // Affichage de l'adresse MAC pour debug
  Serial.print("Adresse MAC: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", macAddress[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.printf(" | Groupe: %d | Version: %s\n", setupTubeNumber, VERSION);
  
  // Envoyer directement à l'émetteur si on a son adresse MAC
  uint8_t* targetAddress;
  if (hasSenderAddress) {
    // Vérifier d'abord si le peer existe déjà (évite l'erreur -8)
    if (!esp_now_is_peer_exist(senderMacAddress)) {
      // Ajouter l'émetteur comme peer pour pouvoir lui répondre directement
      // Utiliser detectedChannel au lieu de canal 1 en dur
      int peerResult = esp_now_add_peer(senderMacAddress, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);
      if (peerResult != 0) {
        Serial.printf("Erreur ajout peer émetteur: %d\n", peerResult);
        // En cas d'erreur, fallback sur broadcast
        targetAddress = broadcastAddress;
        Serial.println("Fallback: envoi en broadcast");
      } else {
        targetAddress = senderMacAddress;
        Serial.println("Peer ajouté, envoi direct à l'émetteur");
      }
    } else {
      targetAddress = senderMacAddress;
      Serial.println("Peer existe déjà, envoi direct à l'émetteur");
    }
  } else {
    targetAddress = broadcastAddress;
    Serial.println("Envoi en broadcast");
  }
  
  // Envoi du paquet AVANT l'indication visuelle
  Serial.printf("Envoi réponse MAC sur canal %d vers ", detectedChannel);
  if (targetAddress == broadcastAddress) {
    Serial.println("BROADCAST");
  } else {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", 
                  targetAddress[0], targetAddress[1], targetAddress[2], 
                  targetAddress[3], targetAddress[4], targetAddress[5]);
  }
  
  uint8_t result = esp_now_send(targetAddress, (uint8_t *) &responsePacket, sizeof(responsePacket));
  
  if (result == 0) {
    Serial.println("Adresse MAC envoyée avec succès");
    // Note: Indication visuelle supprimée pour éviter les conflits FastLED/ESP-NOW
  } else {
    Serial.printf("Erreur envoi MAC: %d (canal actuel: %d)\n", result, wifi_get_channel());
  }
  
  Serial.println("========== FIN ENVOI ADRESSE MAC ==========");
}
// ========== FIN FONCTIONNALITÉ DÉCOUVERTE MAC ==========

// ========== NOUVELLES FONCTIONNALITÉS DE COMMANDE ==========

// Fonction utilitaire pour préparer un paquet de réponse
void prepareResponsePacket(struct_dmx_packet* packet, uint8_t cmdType) {
  packet->blockNumber = 255;  // Indique un paquet de commande
  memset(packet->dmxvalues, 0, sizeof(packet->dmxvalues));
  memset(packet->data, 0, sizeof(packet->data));
  packet->data[0] = SIGNATURE_A;
  packet->data[1] = SIGNATURE_B;
  packet->data[2] = SIGNATURE_C;
  packet->data[3] = cmdType;
  
  // Ajouter l'adresse MAC du récepteur (bytes 4-9)
  uint8_t mac[6];
  WiFi.macAddress(mac);
  for (int i = 0; i < 6; i++) {
    packet->data[4 + i] = mac[i];
  }
}

// Fonction utilitaire pour envoyer un paquet de réponse
void sendResponsePacket(struct_dmx_packet* packet) {
  uint8_t* targetAddress;
  if (hasSenderAddress) {
    if (!esp_now_is_peer_exist(senderMacAddress)) {
      int peerResult = esp_now_add_peer(senderMacAddress, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);
      if (peerResult != 0) {
        targetAddress = broadcastAddress;
      } else {
        targetAddress = senderMacAddress;
      }
    } else {
      targetAddress = senderMacAddress;
    }
  } else {
    targetAddress = broadcastAddress;
  }
  esp_now_send(targetAddress, (uint8_t*)packet, sizeof(struct_dmx_packet));
}

// Réponse INFO complète : toutes les infos du récepteur
void sendInfoResponse() {
  Serial.println("========== ENVOI INFO RESPONSE ==========");
  
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_INFO_CMD);
  
  // data[4-9] : MAC (déjà rempli par prepareResponsePacket)
  responsePacket.data[10] = setupTubeNumber;           // Groupe actuel
  responsePacket.data[11] = VERSION_INT;                // Version firmware (legacy, entier 1-byte)
  responsePacket.data[12] = detectedChannel;           // Canal WiFi actuel
  
  // Nombre de LEDs (uint16_t en little-endian) - utilise la variable globale ledCount
  responsePacket.data[13] = ledCount & 0xFF;
  responsePacket.data[14] = (ledCount >> 8) & 0xFF;
  
  responsePacket.data[15] = maxBrightness;             // Luminosité max
  responsePacket.data[16] = (int8_t)lastRSSI;          // RSSI
  
  // Uptime en secondes (uint32_t en little-endian)
  uint32_t uptime = (millis() - bootTime) / 1000;
  responsePacket.data[17] = uptime & 0xFF;
  responsePacket.data[18] = (uptime >> 8) & 0xFF;
  responsePacket.data[19] = (uptime >> 16) & 0xFF;
  responsePacket.data[20] = (uptime >> 24) & 0xFF;
  
  // Heap libre (uint32_t en little-endian)
  uint32_t freeHeap = ESP.getFreeHeap();
  responsePacket.data[21] = freeHeap & 0xFF;
  responsePacket.data[22] = (freeHeap >> 8) & 0xFF;
  responsePacket.data[23] = (freeHeap >> 16) & 0xFF;
  responsePacket.data[24] = (freeHeap >> 24) & 0xFF;
  
  // Mode actuel (0=RUNNING, 1=SETUP, 2=SCAN, 3=IDENTIFY)
  uint8_t mode = 0;
  if (etat == SETUP || remoteConfigActive) mode = 1;
  else if (isScanning) mode = 2;
  else if (identifyActive) mode = 3;
  responsePacket.data[25] = mode;
  
  // Stats streaming actif
  responsePacket.data[26] = statsStreamActive ? 1 : 0;
  
  // Stats serial enabled
  responsePacket.data[27] = statsSerialEnabled ? 1 : 0;
  
  // Version firmware format YYMMDD-HHMM (max 24 chars), transmise au dashboard
  const char* timestamp = VERSION;
  uint8_t timestampLen = strlen(timestamp);
  if (timestampLen > 24) timestampLen = 24;
  responsePacket.data[28] = timestampLen;  // Longueur du timestamp
  memcpy(&responsePacket.data[29], timestamp, timestampLen);

  // Adresse EEPROM (uint16 LE) + flag runtime — extra[43..45] côté Lite
  responsePacket.data[53] = setupAddress & 0xFF;
  responsePacket.data[54] = (setupAddress >> 8) & 0xFF;
  responsePacket.data[55] = multifixtureEnabled ? 1 : 0;

  // Énergie cumulée en mWh (uint32 LE) et puissance instantanée en mW (uint32 LE)
  uint32_t energy = powerBudgetEnergyMwh();
  responsePacket.data[56] = energy & 0xFF;
  responsePacket.data[57] = (energy >> 8) & 0xFF;
  responsePacket.data[58] = (energy >> 16) & 0xFF;
  responsePacket.data[59] = (energy >> 24) & 0xFF;
  uint32_t instantMw = powerBudgetInstantMw();
  responsePacket.data[60] = instantMw & 0xFF;
  responsePacket.data[61] = (instantMw >> 8) & 0xFF;
  responsePacket.data[62] = (instantMw >> 16) & 0xFF;
  responsePacket.data[63] = (instantMw >> 24) & 0xFF;
  uint16_t limitW = powerBudgetLimitW();
  responsePacket.data[64] = limitW & 0xFF;
  responsePacket.data[65] = (limitW >> 8) & 0xFF;
  
  sendResponsePacket(&responsePacket);
  
  Serial.printf("Info envoyée | Groupe:%d | Canal:%d | LEDs:%d | RSSI:%d | FW:%s\n",
                setupTubeNumber, detectedChannel, ledCount, lastRSSI, VERSION_TIMESTAMP);
  Serial.println("========== FIN ENVOI INFO RESPONSE ==========");
}

void sendDmxAddressResponse() {
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_DMX_ADDRESS_CMD);
  responsePacket.data[10] = setupAddress & 0xFF;
  responsePacket.data[11] = (setupAddress >> 8) & 0xFF;
  responsePacket.data[12] = multifixtureEnabled ? 1 : 0;
  sendResponsePacket(&responsePacket);
}

// Réponse CHANNEL : canal WiFi actuel
void sendChannelResponse() {
  Serial.println("========== ENVOI CHANNEL RESPONSE ==========");
  
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_CHANNEL_CMD);
  
  responsePacket.data[10] = detectedChannel;  // Canal actuel
  responsePacket.data[11] = wifi_get_channel(); // Canal WiFi réel
  
  sendResponsePacket(&responsePacket);
  
  Serial.printf("Canal envoyé: détecté=%d, réel=%d\n", detectedChannel, wifi_get_channel());
  Serial.println("========== FIN ENVOI CHANNEL RESPONSE ==========");
}

// Réponse SCAN : résultats du scan de canaux
void sendScanResponse() {
  Serial.println("========== ENVOI SCAN RESPONSE ==========");
  
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_SCAN_CMD);
  
  responsePacket.data[10] = scanBestChannel;  // Meilleur canal trouvé
  responsePacket.data[11] = 13;               // Nombre de canaux scannés
  
  // Paquets reçus par canal (canaux 1-13)
  for (int i = 1; i <= 13; i++) {
    responsePacket.data[11 + i] = scanResultsPerChannel[i];
  }
  
  sendResponsePacket(&responsePacket);
  
  Serial.printf("Résultats scan envoyés | Meilleur canal: %d\n", scanBestChannel);
  Serial.println("========== FIN ENVOI SCAN RESPONSE ==========");
}

// Réponse STATS : statistiques périodiques
void sendStatsResponse() {
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_STATS_CMD);
  
  // Paquets DMX reçus depuis dernier rapport (uint32_t en little-endian)
  responsePacket.data[10] = statsStreamPacketCount & 0xFF;
  responsePacket.data[11] = (statsStreamPacketCount >> 8) & 0xFF;
  responsePacket.data[12] = (statsStreamPacketCount >> 16) & 0xFF;
  responsePacket.data[13] = (statsStreamPacketCount >> 24) & 0xFF;
  
  responsePacket.data[14] = (int8_t)lastRSSI;     // RSSI actuel
  responsePacket.data[15] = detectedChannel;       // Canal WiFi
  
  // Uptime en secondes (uint32_t en little-endian)
  uint32_t uptime = (millis() - bootTime) / 1000;
  responsePacket.data[16] = uptime & 0xFF;
  responsePacket.data[17] = (uptime >> 8) & 0xFF;
  responsePacket.data[18] = (uptime >> 16) & 0xFF;
  responsePacket.data[19] = (uptime >> 24) & 0xFF;
  
  responsePacket.data[20] = setupTubeNumber;       // Groupe actuel
  
  sendResponsePacket(&responsePacket);
  
  // Reset du compteur après envoi
  statsStreamPacketCount = 0;
}

// Réponse ACK : accusé de réception simple
void sendAckResponse(uint8_t originalCmd, uint8_t status) {
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_ACK_CMD);
  
  responsePacket.data[10] = originalCmd;  // Commande à laquelle on répond
  responsePacket.data[11] = status;       // 0 = OK, autre = erreur
  
  sendResponsePacket(&responsePacket);
  Serial.printf("ACK envoyé pour cmd 0x%02X, status: %d\n", originalCmd, status);
}

// Réponse PONG : réponse au ping
void sendPongResponse() {
  struct_dmx_packet responsePacket;
  prepareResponsePacket(&responsePacket, RESPONSE_PONG_CMD);
  
  responsePacket.data[10] = (int8_t)lastRSSI;  // RSSI
  responsePacket.data[11] = detectedChannel;   // Canal
  
  // Timestamp actuel (pour calcul latence si besoin)
  uint32_t timestamp = millis();
  responsePacket.data[12] = timestamp & 0xFF;
  responsePacket.data[13] = (timestamp >> 8) & 0xFF;
  responsePacket.data[14] = (timestamp >> 16) & 0xFF;
  responsePacket.data[15] = (timestamp >> 24) & 0xFF;
  
  sendResponsePacket(&responsePacket);
  Serial.println("PONG envoyé");
}

// Fonction pour lancer un scan complet des canaux avec statistiques
void performChannelScanWithStats() {
  Serial.println("========== SCAN DES CANAUX AVEC STATS ==========");
  isScanning = true;
  
  // Réinitialiser les résultats
  memset(scanResultsPerChannel, 0, sizeof(scanResultsPerChannel));
  scanBestChannel = 0;
  uint8_t maxPackets = 0;
  
  // Sauvegarder le canal actuel
  uint8_t originalChannel = detectedChannel;
  
  // Scanner chaque canal pendant 500ms
  for (uint8_t ch = 1; ch <= 13; ch++) {
    Serial.printf("Scan canal %d...\n", ch);
    
    // Réinitialiser ESP-NOW pour ce canal
    esp_now_deinit();
    wifi_set_channel(ch);
    delay(50);
    
    if (esp_now_init() != 0) {
      Serial.printf("Erreur init ESP-NOW sur canal %d\n", ch);
      continue;
    }
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    
    // Compter les paquets pendant 500ms
    channelDetected = false;
    uint8_t packetCount = 0;
    unsigned long scanStart = millis();
    
    // Utiliser le callback de scan temporaire
    esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) {
      // Vérifier la signature PoulpyLights
      if (len >= 132 && data[129] == SIGNATURE_A && data[130] == SIGNATURE_B && data[131] == SIGNATURE_C) {
        channelDetected = true;
      }
    });
    
    // Attendre et compter
    while (millis() - scanStart < 500) {
      if (channelDetected) {
        packetCount++;
        channelDetected = false;
      }
      yield();
    }
    
    scanResultsPerChannel[ch] = packetCount;
    Serial.printf("Canal %d: %d paquets\n", ch, packetCount);
    
    if (packetCount > maxPackets) {
      maxPackets = packetCount;
      scanBestChannel = ch;
    }
    
    // Feedback visuel
    leds[0] = CRGB::Blue;
    leds[ch] = (packetCount > 0) ? CRGB::Green : CRGB::Red;
    poulpyShow();
  }
  
  // Restaurer le canal original ou utiliser le meilleur
  if (scanBestChannel > 0) {
    detectedChannel = scanBestChannel;
    saveDetectedChannel(detectedChannel);
  } else {
    detectedChannel = originalChannel;
  }
  
  // Réinitialiser ESP-NOW sur le canal choisi
  esp_now_deinit();
  wifi_set_channel(detectedChannel);
  delay(100);
  
  if (esp_now_init() == 0) {
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    extern void OnDataRecv(uint8_t*, uint8_t*, uint8_t);
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);
  }
  
  isScanning = false;
  
  // Afficher le résultat
  if (scanBestChannel > 0) {
    for (int i = 0; i < min((int)scanBestChannel, (int)ledCount); i++) {
      leds[i] = CRGB::Green;
    }
  } else {
    leds[0] = CRGB::Red;
  }
  poulpyShow();
  delay(1000);
  
  Serial.printf("Scan terminé | Meilleur canal: %d avec %d paquets\n", scanBestChannel, maxPackets);
  Serial.println("========== FIN SCAN DES CANAUX ==========");
  
  // Envoyer les résultats
  sendScanResponse();
}

// Fonction pour le mode identification (clignotement blanc)
void handleIdentifyMode() {
  if (!identifyActive) return;
  
  if (millis() > identifyEndTime) {
    identifyActive = false;
    // Éteindre les LEDs
    fill_solid(leds, ledCount, CRGB::Black);
    poulpyShow();
    Serial.println("Fin mode identification");
    return;
  }
  
  // Clignotement rapide en blanc
  static unsigned long lastBlink = 0;
  static bool blinkOn = false;
  
  if (millis() - lastBlink > 200) {
    blinkOn = !blinkOn;
    fill_solid(leds, ledCount, blinkOn ? CRGB::White : CRGB::Black);
    poulpyShow();
    lastBlink = millis();
  }
}

// Traitement des nouvelles commandes
void handleNewCommand(uint8_t cmd, uint8_t* data, uint8_t len) {
  Serial.printf("Traitement nouvelle commande: 0x%02X\n", cmd);
  
  switch (cmd) {
    case GET_INFO_CMD:
      sendInfoResponse();
      break;
      
    case GET_CHANNEL_CMD:
      sendChannelResponse();
      break;
      
    case START_SCAN_CMD:
      performChannelScanWithStats();
      break;
      
    case SET_CHANNEL_CMD:
      // data[10] contient le nouveau canal WiFi
      if (len >= 11) {
        uint8_t newChannel = data[10];
        if (newChannel >= 1 && newChannel <= 13) {
          Serial.printf("Commande SET_CHANNEL reçue: canal %d\n", newChannel);
          
          // Vérifier si le canal a changé
          if (newChannel != detectedChannel) {
            Serial.printf("Changement de canal: %d -> %d\n", detectedChannel, newChannel);
            
            // Sauvegarder le nouveau canal en EEPROM
            saveDetectedChannel(newChannel);
            
            // Envoyer un ACK avant le redémarrage
            sendAckResponse(SET_CHANNEL_CMD, 0);  // 0 = OK
            delay(200);  // Petit délai pour que l'ACK parte
            
            // Redémarrer pour appliquer le nouveau canal
            Serial.println("Redémarrage pour appliquer le nouveau canal...");
            delay(500);
            ESP.restart();
          } else {
            Serial.println("Canal identique, pas de changement nécessaire");
            sendAckResponse(SET_CHANNEL_CMD, 0);  // 0 = OK
          }
        } else {
          Serial.printf("Canal invalide: %d (doit être entre 1 et 13)\n", newChannel);
          sendAckResponse(SET_CHANNEL_CMD, 2);  // 2 = valeur invalide
        }
      } else {
        Serial.println("Paquet SET_CHANNEL trop court");
        sendAckResponse(SET_CHANNEL_CMD, 1);  // 1 = erreur
      }
      break;
      
    case SET_DMX_ADDRESS_CMD:
      if (len >= 12) {
        uint16_t addr = data[10] | (data[11] << 8);
        if (addr >= 1 && addr <= (512 - MULTIFIXTURE_SLOT + 1)) {
          setupAddress = addr;
          EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS, setupAddress & 0xFF);
          EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS + 1, (setupAddress >> 8) & 0xFF);
          EEPROM.write(EEPROM_ADDR_MULTIFIX_INIT, MULTIFIX_INIT_MAGIC);
          EEPROM.commit();
          Serial.printf("Adresse DMX multifixure: %d\n", setupAddress);
          sendAckResponse(SET_DMX_ADDRESS_CMD, 0);
        } else {
          sendAckResponse(SET_DMX_ADDRESS_CMD, 2);
        }
      }
      break;

    case SET_MULTIFIXTURE_CMD:
      if (len >= 11) {
        bool want = data[10] != 0;
        if (want != multifixtureEnabled)
          multifixtureEnabled = want;
        if (multifixtureEnabled)
          lastMultifixureMs = millis();
        sendAckResponse(SET_MULTIFIXTURE_CMD, 0);
      } else {
        sendAckResponse(SET_MULTIFIXTURE_CMD, 1);
      }
      break;

    case GET_DMX_ADDRESS_CMD:
      sendDmxAddressResponse();
      break;

    case SET_LED_COUNT_CMD:
      // data[10-11] contient le nouveau nombre de LEDs (uint16_t en little-endian)
      if (len >= 12) {
        uint16_t newLedCount = data[10] | (data[11] << 8);
        if (newLedCount > 0 && newLedCount <= MAXLEDLENGTH) {
          // Sauvegarder en EEPROM et redémarrer pour appliquer
          EEPROM.write(EEPROM_ADDR_LED_COUNT, newLedCount & 0xFF);
          EEPROM.write(EEPROM_ADDR_LED_COUNT + 1, (newLedCount >> 8) & 0xFF);
          EEPROM.commit();
          Serial.printf("Nombre de LEDs changé: %d -> Sauvegardé en EEPROM, redémarrage...\n", newLedCount);
          sendAckResponse(SET_LED_COUNT_CMD, 0);  // 0 = OK
          delay(500);
          ESP.restart();
        } else {
          Serial.printf("Nombre de LEDs invalide: %d (max: %d)\n", newLedCount, MAXLEDLENGTH);
          sendAckResponse(SET_LED_COUNT_CMD, 2);  // 2 = valeur invalide
        }
      }
      break;
      
    case START_STATS_CMD:
      statsStreamActive = true;
      statsStreamPacketCount = 0;
      lastStatsStreamTime = millis();
      Serial.println("Envoi stats périodiques ACTIVÉ");
      sendAckResponse(START_STATS_CMD, 0);
      break;
      
    case STOP_STATS_CMD:
      statsStreamActive = false;
      Serial.println("Envoi stats périodiques DÉSACTIVÉ");
      sendAckResponse(STOP_STATS_CMD, 0);
      break;
      
    case SET_BRIGHTNESS_CMD:
      if (len >= 11) {
        maxBrightness = data[10];
        FastLED.setBrightness(maxBrightness);
        Serial.printf("Luminosité max définie: %d\n", maxBrightness);
        sendAckResponse(SET_BRIGHTNESS_CMD, 0);
      }
      break;
      
    case RESTART_SINGLE_CMD:
      Serial.println("Redémarrage demandé...");
      sendAckResponse(RESTART_SINGLE_CMD, 0);
      delay(500);
      ESP.restart();
      break;
      
    case SAVE_CONFIG_CMD:
      // Sauvegarder la config actuelle en EEPROM
      EEPROM.write(EEPROM_ADDR_SETUP_TUBE, setupTubeNumber);
      EEPROM.write(EEPROM_ADDR_DETECTED_CHANNEL, detectedChannel);
      // Sauvegarder ledCount (uint16_t = 2 bytes)
      EEPROM.write(EEPROM_ADDR_LED_COUNT, ledCount & 0xFF);
      EEPROM.write(EEPROM_ADDR_LED_COUNT + 1, (ledCount >> 8) & 0xFF);
      EEPROM.commit();
      Serial.printf("Configuration sauvegardée en EEPROM (groupe=%d, canal=%d, leds=%d)\n", 
                    setupTubeNumber, detectedChannel, ledCount);
      sendAckResponse(SAVE_CONFIG_CMD, 0);
      break;
      
    case IDENTIFY_CMD:
      identifyActive = true;
      identifyEndTime = millis() + 3000;  // 3 secondes de clignotement
      Serial.println("Mode identification activé");
      sendAckResponse(IDENTIFY_CMD, 0);
      break;
      
    case PING_CMD:
      sendPongResponse();
      break;

    case RESET_ENERGY_CMD:
      powerBudgetReset();
      sendAckResponse(RESET_ENERGY_CMD, 0);
      break;

    case SET_POWER_LIMIT_CMD:
      if (len >= 12) {
        uint16_t watts = data[10] | (data[11] << 8);
        if (watts <= 200) {
          powerBudgetSetLimitW(watts);
          sendAckResponse(SET_POWER_LIMIT_CMD, 0);
        } else {
          sendAckResponse(SET_POWER_LIMIT_CMD, 2);
        }
      } else {
        sendAckResponse(SET_POWER_LIMIT_CMD, 1);
      }
      break;
      
    case SET_STATS_SERIAL_CMD:
      // data[10] contient 1 pour activer, 0 pour désactiver
      if (len >= 11) {
        statsSerialEnabled = (data[10] != 0);
        Serial.printf("Stats serial: %s\n", statsSerialEnabled ? "ACTIVÉ" : "DÉSACTIVÉ");
        sendAckResponse(SET_STATS_SERIAL_CMD, 0);
      } else {
        sendAckResponse(SET_STATS_SERIAL_CMD, 1);  // Erreur
      }
      break;
      
    default:
      Serial.printf("Commande inconnue: 0x%02X\n", cmd);
      sendAckResponse(cmd, 255);  // 255 = commande inconnue
      break;
  }
}

// ========== FIN NOUVELLES FONCTIONNALITÉS ==========

// ========== FONCTIONNALITÉ SETUP À DISTANCE ==========

// Fonction pour entrer en mode setup à distance
void enterRemoteSetup() {
  Serial.println("========== ENTRÉE SETUP À DISTANCE ==========");
  //Serial.println("DEBUG: Début enterRemoteSetup");
  
  // Test minimal - juste les variables essentielles
  lastConfigBlink = millis();
  configBlinkState = false;
  
  //Serial.println("DEBUG: Variables initialisées");
  Serial.printf("Mode setup à distance activé | Groupe actuel: %d\n", setupTubeNumber);
  //Serial.println("DEBUG: Fin enterRemoteSetup");
}

// Fonction pour sortir du mode setup à distance
void exitRemoteSetup() {
  Serial.println("========== SORTIE SETUP À DISTANCE ==========");
  //Serial.println("DEBUG: Début exitRemoteSetup");
  
  // Sauvegarde du numéro de groupe en EEPROM
  //Serial.println("DEBUG: Avant EEPROM.write");
  EEPROM.write(EEPROM_ADDR_SETUP_TUBE, setupTubeNumber);
  //Serial.println("DEBUG: Avant EEPROM.commit");
  bool success = EEPROM.commit();
  //Serial.println("DEBUG: Après EEPROM.commit");
  
  Serial.printf("Sauvegarde groupe %d en EEPROM: %s\n", 
                setupTubeNumber, 
                success ? "OK" : "ERREUR");
  
  // Note: remoteConfigActive est déjà désactivé dans OnDataRecv avant l'appel de cette fonction
  //Serial.println("DEBUG: Fin exitRemoteSetup");
  Serial.println("========== FIN SORTIE SETUP À DISTANCE ==========");
}

// Fonction pour définir un nouveau numéro de groupe
void setGroupNumber(uint8_t newGroupNumber) {
  if (newGroupNumber <= 31) { // Limite sécuritaire
    setupTubeNumber = newGroupNumber;
    Serial.printf("Nouveau groupe défini: %d\n", setupTubeNumber);
    
    // Reset du clignotement pour affichage immédiat
    lastBlinkTime = millis();
    blinkState = false;
  } else {
    Serial.printf("ERREUR: Numéro de groupe invalide: %d (max 31)\n", newGroupNumber);
  }
}

// Fonction principale pour gérer l'affichage en mode setup à distance
void handleRemoteSetup() {
  // Gestion du clignotement (toutes les 200ms)
  if (millis() - lastBlinkTime >= 200) {
    lastBlinkTime = millis();
    blinkState = !blinkState;
  }
  
  // Effacer tout le strip
  FastLED.clear();
  
  // LEDs 100+ en blanc pour signaler la sélection
  for (int j = 100; j < ledCount; j++) {
    leds[j] = CRGB::White;
  }
  
  // Affichage du numéro de groupe (seulement si clignotement actif)
  if (blinkState) {
    if (setupTubeNumber == 0) {
      // Groupe 0 : première LED en rouge
      if (0 < ledCount) {
        leds[0] = CRGB::Red;
      }
    } else {
      // Groupe n : n LEDs vertes espacées de 10
      for (int j = 0; j < setupTubeNumber && (j * 10) < min(100, (int)ledCount); j++) {
        leds[j * 10] = CRGB::Green;
      }
    }
  }
  
  // Afficher les LEDs
  poulpyShow();
}

// ========== FIN FONCTIONNALITÉ SETUP À DISTANCE ==========

// ========== CALLBACK TEMPORAIRE POUR SCAN CANAL ==========
// Callback utilisé uniquement pendant le scan pour détecter les paquets PoulpyLights
void OnScanReceive(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  // IMPORTANT: Ce callback doit être ULTRA-LÉGER pour éviter les WDT reset
  // Pas de Serial.print ici ! Les paquets arrivent très vite (~120/sec)
  
  // Vérifier la taille d'un paquet DMX
  if (len == sizeof(struct_dmx_packet))
  {
    // Vérifier uniquement la signature PoulpyLights (sans copier tout le paquet)
    // La signature est dans data[0..2], qui est à l'offset sizeof(blockNumber) + sizeof(dmxvalues)
    // = 1 + 128 = 129 bytes après le début
    uint8_t* dataPtr = incomingData + 1 + 128; // Pointer vers data[]
    
    if (dataPtr[0] == SIGNATURE_A &&
        dataPtr[1] == SIGNATURE_B &&
        dataPtr[2] == SIGNATURE_C)
    {
      // Signature valide détectée ! Juste mettre le flag
      channelDetected = true;
      memcpy(senderMacFromScan, mac, 6);
    }
  }
}
// ========== FIN CALLBACK SCAN CANAL ==========

// Callback when data is received
// à chaque fois qu'un bloc de 128 valeurs est reçu par ESP_NOW, on met à jour ces valeurs dans le tableau dmxChannels
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
  // Mettre à jour le timestamp du dernier paquet reçu (pour la surveillance du timeout)
  lastPacketTime = millis();
  
  // Collecte des statistiques
  statsTotalPackets++;
  // RSSI approximatif (WiFi.RSSI() ne donne pas le RSSI du paquet ESP-NOW mais de la connexion STA)
  // Sur ESP8266, il n'y a pas de moyen simple d'obtenir le RSSI des paquets ESP-NOW
  lastRSSI = WiFi.RSSI();
  
  // Vérification de la taille d'un paquet DMX
  if (len == sizeof(struct_dmx_packet))
  {
    memcpy(&incomingDMXPacket, incomingData, sizeof(incomingDMXPacket));

    // Vérifier la signature pour valider le paquet
    if (incomingDMXPacket.data[0] == SIGNATURE_A &&
        incomingDMXPacket.data[1] == SIGNATURE_B &&
        incomingDMXPacket.data[2] == SIGNATURE_C)
    {
      // ========== GESTION COMMANDES SPÉCIALES ==========
      
      // Vérifier si c'est un paquet de commande OTA
      if (incomingDMXPacket.data[3] == OTA_UPDATE_CMD)
      {
        statsOTAPackets++;
        Serial.println("Commande OTA détectée !");
        handleOTAUpdate(incomingDMXPacket.data, incomingDMXPacket.dmxvalues);
        return; // Sortir immédiatement après traitement OTA
      }
      
      // Vérifier si c'est une demande d'adresse MAC
      if (incomingDMXPacket.data[3] == REQUEST_MAC_CMD)
      {
        statsMACPackets++;
        Serial.printf("Demande d'adresse MAC détectée ! (canal: %d)\n", wifi_get_channel());
        
        // Stocker l'adresse MAC de l'émetteur pour pouvoir lui répondre directement
        memcpy(senderMacAddress, mac, 6);
        hasSenderAddress = true;
        
        Serial.printf("Émetteur MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        sendMACResponse();
        return; // Sortir immédiatement après envoi de la réponse
      }
      
      // Vérifier si c'est une réponse d'adresse MAC
      if (incomingDMXPacket.data[3] == RESPONSE_MAC_CMD)
      {
        statsMACPackets++;
        DEBUG_PRINTLN("Réponse MAC reçue");
      }
      
      // Vérifier si c'est une commande d'entrée en setup à distance
      if (incomingDMXPacket.data[3] == ENTER_REMOTE_SETUP_CMD && isCommandForMe(incomingDMXPacket.data))
      {
        statsSETUPPackets++;
        Serial.println("Commande ENTER_REMOTE_SETUP détectée pour moi !");
        remoteConfigActive = true;
        enterRemoteSetup();
        return; // Sortir immédiatement après traitement
      }
      
      // Vérifier si c'est une commande de sortie du setup à distance
      if (incomingDMXPacket.data[3] == EXIT_REMOTE_SETUP_CMD && isCommandForMe(incomingDMXPacket.data))
      {
        statsSETUPPackets++;
        Serial.println("Commande EXIT_REMOTE_SETUP détectée pour moi !");
        remoteConfigActive = false;
        exitRemoteSetup();
        return; // Sortir immédiatement après traitement
      }
      
      // Vérifier si c'est une commande de définition de groupe
      if (incomingDMXPacket.data[3] == SET_GROUP_NUMBER_CMD && isCommandForMe(incomingDMXPacket.data))
      {
        statsSETUPPackets++;
        uint8_t newGroup = incomingDMXPacket.data[10]; // Le groupe est maintenant en position 10
        Serial.printf("Commande SET_GROUP_NUMBER détectée : %d\n", newGroup);
        setGroupNumber(newGroup);
        return; // Sortir immédiatement après traitement
      }
      
      // Vérifier si c'est une commande PANIC RESTART
      if (incomingDMXPacket.data[3] == PANIC_RESTART_CMD)
      {
        Serial.println("Commande PANIC_RESTART détectée ! Redémarrage immédiat...");
        delay(100); // Petit délai pour que le message s'affiche
        ESP.restart(); // Redémarrage immédiat
        return;
      }

      // Déclenchement animation : ne pas fusionner les blocs DMX (buffer inchangé)
      if (incomingDMXPacket.data[3] == ANIMATION_TRIGGER_CMD)
      {
        armAnimationTrigger(incomingDMXPacket.data[10]);
        return;
      }
      
      // ========== NOUVELLES COMMANDES (v67+) ==========
      uint8_t cmdCode = incomingDMXPacket.data[3];
      
      // Vérifier si c'est une des nouvelles commandes (plage 0xE0-0xEF)
      if ((cmdCode >= 0xE0 && cmdCode <= 0xEF) || cmdCode == RESET_ENERGY_CMD || cmdCode == SET_POWER_LIMIT_CMD) {
        // Stocker l'adresse MAC de l'émetteur pour pouvoir lui répondre
        memcpy(senderMacAddress, mac, 6);
        hasSenderAddress = true;
        
        // SET_CHANNEL_CMD est toujours en broadcast (0xFF dans data[4..9])
        // Il faut la traiter même si isCommandForMe() retourne false
        if (cmdCode == SET_CHANNEL_CMD) {
          Serial.println("Commande SET_CHANNEL_CMD détectée (broadcast)");
          handleNewCommand(cmdCode, incomingDMXPacket.data, sizeof(incomingDMXPacket.data));
          return;
        }
        
        // Pour les autres commandes, vérifier si elles sont destinées à ce récepteur
        if (isCommandForMe(incomingDMXPacket.data)) {
          handleNewCommand(cmdCode, incomingDMXPacket.data, sizeof(incomingDMXPacket.data));
        }
        return;
      }
      // ========== FIN NOUVELLES COMMANDES ==========
      
      // ========== FIN GESTION COMMANDES SPÉCIALES ==========

      // Traitement normal des paquets DMX (si data[3] != OTA_UPDATE_CMD)
      uint8_t packetNumber = incomingDMXPacket.blockNumber;
      if (packetNumber < 4)
      {
        statsDMXPackets++; // Incrémenter le compteur DMX (toujours, même en mode config)
        statsStreamPacketCount++; // Incrémenter le compteur pour stats streaming
        
        // IGNORER l'application des valeurs DMX si en mode configuration à distance
        // mais on a quand même compté le paquet pour les statistiques
        if (!remoteConfigActive) {
          for (int i = 0; i < 128; i++)
          {
            dmxChannels[(packetNumber * 128) + i] = incomingDMXPacket.dmxvalues[i];
          }
          const bool wantMulti =
              incomingDMXPacket.data[STREAM_KIND] == STREAM_MULTIFIXTURE
              && incomingDMXPacket.data[STREAM_MULTIFIXTURE_MARK] == STREAM_MULTIFIXTURE_MAGIC;
          if (wantMulti != multifixtureEnabled)
            multifixtureEnabled = wantMulti;
          if (multifixtureEnabled)
            lastMultifixureMs = millis();
        }
      }
    } else {
      statsInvalidPackets++;
      DEBUG_PRINTF("Paquet invalide: signature incorrecte\n");
    }
  } else {
    statsInvalidPackets++;
    DEBUG_PRINTF("ERREUR: Taille de paquet invalide: %d (attendu: %d)\n", len, sizeof(struct_dmx_packet));
  }
}

struct Color
{ // type utilisé par la fonction convertToColor
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

Color convertToColor(uint8_t index)
{ // fonction permettant de mapper les couleurs de manière continue sur 256 valeurs (pour pouvoir utilier un seul canal DMX pour représenter une couleur dans certains modes)
  Color color;

  if (index < 85)
  {
    color.r = index * 3;
    color.g = 255 - index * 3;
    color.b = 0;
  }
  else if (index < 170)
  {
    index -= 85;
    color.r = 255 - index * 3;
    color.g = 0;
    color.b = index * 3;
  }
  else
  {
    index -= 170;
    color.r = 0;
    color.g = index * 3;
    color.b = 255 - index * 3;
  }

  return color;
}



void flickering(uint8_t dmx1, uint8_t dmx2, uint8_t dmx3, uint8_t dmx4, uint8_t dmx5, uint8_t dmx6, uint8_t dmx7, uint8_t dmx8,uint8_t dmx9,uint8_t dmx10)
{
  // "grésillement lumineux"
// 2 couleur unie ou par groupe 
// 3 longueur minimale 
// 4 longueur maximale 
// 5 durée d'une séquence de flickering 
// 6 durée d'une séquence éteinte 
// 7 durée d'un flicker individuel 
// 8 vitesse changement de pattern (pas encore utilisé ici) 
// 9 10 11 rgb grp0 | 12 13 14 rgb grp1 | etc.
{
  // 1) Choix des canaux RGB selon "couleur unie (0)" ou "par groupe (1)"

  // 2) Calcul des longueurs mini/maxi pour la construction de segments
  int minimumSegmentLength = max(1, (dmx2* (int)ledCount) / 255);
  int maximumSegmentLength = max(1, (dmx3 * (int)ledCount) / 255);
  // Si jamais le max < min, on les inverse
  if (maximumSegmentLength < minimumSegmentLength) {
    int temp = maximumSegmentLength;
    maximumSegmentLength = minimumSegmentLength;
    minimumSegmentLength = temp;
  }

  // 3) Durée entre deux régénérations de pattern
  unsigned long dureeFlickerPattern = random(1, 100UL * dmx7);

  // => Régénération du pattern si on a dépassé cette durée
  if (millis() - lastUpdateFlickerPattern >= dureeFlickerPattern) {
    lastUpdateFlickerPattern = millis();

    // On construit un nouveau "pattern" de segments allumés/éteints dans flickerLeds[]
    bool allumeSegment = (random(2) == 1);  // random(2) => 0 ou 1
    for (int j = 0; j < ledCount; ) {
      int segLen = random(minimumSegmentLength, maximumSegmentLength);
      // On s'assure de ne pas dépasser la fin du strip
      if (j + segLen > ledCount) {
        segLen = ledCount - j; 
      }

      // On remplit segLen pixels
      for (int k = 0; k < segLen; k++) {
        if (allumeSegment) {
          flickerLeds[j + k].r = dmx8;
          flickerLeds[j + k].g = dmx9;
          flickerLeds[j + k].b = dmx10;
        } else {
          flickerLeds[j + k] = CRGB::Black;
        }
      }

      // On alterne la phase (allumé/éteint)
      allumeSegment = !allumeSegment;

      // Avance de segLen
      j += segLen;
    }
  }

  // 4) Paramètres de la partie “flickering bloquant”
  unsigned long dureeAllume = random(5UL * dmx4, 10UL * dmx4);   // temps pendant lequel on va faire des flickers on/off
  unsigned long dureeEteint = random(5UL * dmx5, 10UL * dmx5);   // temps “éteint”
  unsigned long flickerMin  =  dmx6/10;   // min du flicker
  unsigned long flickerMax  =  dmx6;   // max du flicker

  // On “bloque” le programme pour ce temps-là
  debutAllume = millis();
  while (millis() - debutAllume <= dureeAllume) {
    // A chaque tour, on choisit une durée de flicker
    lastFlicker = millis();
    unsigned long dureeFlicker = random(flickerMin, flickerMax + 1);

    // On alterne allumé / éteint 
    static bool isOn = false;
    isOn = !isOn;

    if (isOn) {
      // On copie le pattern calculé dans flickerLeds[]
      for (int i = 0; i < ledCount; i++) {
        leds[i] = flickerLeds[i];
      }
    } else {
      FastLED.clear();
    }
    poulpyShow();

    // On attend la fin du petit flicker
    while (millis() - lastFlicker < dureeFlicker) {
      delay(1);  
    }
  }

  // 5) Ensuite, on reste éteint un temps aléatoire dans [dureeEteint/2..dureeEteint]
  if (dureeEteint > 0) {
    delay(random(dureeEteint / 2, dureeEteint));
  }
  
}
}


// Contrôle du ledstrip proprement dit, en fonction des valeurs DMX
// !!! POUR AJOUTER UN MODE !!!
// Il suffit d'ajouter un bloc "CASE"
void DMX2LEDSTRIP()
{
  if (multifixtureEnabled) {
    if (millis() - lastMultifixureMs > MULTIFIXTURE_TIMEOUT_MS) {
      multifixtureEnabled = false;
      FastLED.clear();
      poulpyShow();
      return;
    }
    if (renderMultifixture(dmxChannels, setupAddress, ledCount))
      return;
    FastLED.clear();
    poulpyShow();
    return;
  }

  FastLED.clear();

  setupMode = dmxChannels[0]; // le canal 1 précise le mode des ledstrips

  if (renderExtendedLedMode((uint8_t)setupMode)) {
    poulpyShow();
    delay(1);
    return;
  }

  int ir = 3 * setupTubeNumber + 1;
  int ig = 3 * setupTubeNumber + 2;
  int ib = 3 * setupTubeNumber + 3;

// Offset utilisé par les modes rainbow (1, 2, 24)
if (setupMode != 1 && setupMode != 2 && setupMode != 24) {
    hueOffset = 0;
}


  switch (setupMode)
  {
  case 0: // Une seule couleur pour tous les pixels et tous les groupes. Les canaux 2 3 4 représentent les composantes R G B

    for (int j = 0; j < ledCount; j++)
    {
      leds[j].r = dmxChannels[1];
      leds[j].g = dmxChannels[2];
      leds[j].b = dmxChannels[3];
    }
    break;

  




case 1: // Mode Rainbow défilant avec contrôle du décalage, de la densité des couleurs (distance entre chaque couleur) et de la vitesse de défilement
{
    uint8_t colorDensity = dmxChannels[2]/2; // Canal 3 pour la densité des couleurs (distance entre pixels de même couleur)
    uint8_t rainbowSpeed = dmxChannels[3]; // Canal 4 pour la vitesse de déplacement
    
    

    // Calculer la densité des couleurs
    uint8_t deltaHue = map(colorDensity, 0, 255, 1, 50); // Plus colorDensity est élevé, plus les couleurs sont espacées

    // Générer un arc-en-ciel avec la densité contrôlée
    fill_rainbow(leds, ledCount, (hueOffset + (setupTubeNumber * dmxChannels[1])) % 255, deltaHue);
    //fill_rainbow(leds, ledCount, hueOffset+(setupTubeNumber*dmxChannels[1]), deltaHue);

    // Ajuster l'offset en fonction de la vitesse
    hueOffset += map(rainbowSpeed, 0, 255, -5, 5); // Contrôle de la vitesse et de la direction (-5 à 5)

    // Afficher les LEDs
    poulpyShow();
    break;
}

case 2: // Mode Rainbow avec intensité ajustable
{
    uint8_t colorDensity = dmxChannels[2] / 2; // Canal 3 pour la densité des couleurs (distance entre pixels de même couleur)
    uint8_t rainbowSpeed = dmxChannels[3];    // Canal 4 pour la vitesse de déplacement
    uint8_t intensity = dmxChannels[4];       // Canal 5 pour l’intensité générale des LEDs (0–255)

    // Calculer la densité des couleurs
    uint8_t deltaHue = map(colorDensity, 0, 255, 1, 50); // Plus colorDensity est élevé, plus les couleurs sont espacées

    // Générer un arc-en-ciel avec la densité contrôlée
    fill_rainbow(leds, ledCount, (hueOffset + (setupTubeNumber * dmxChannels[1])) % 255, deltaHue);

    // Appliquer l'intensité générale
    for (int i = 0; i < ledCount; i++) {
        leds[i].fadeLightBy(255 - intensity); // Réduire la luminosité en fonction de l'intensité
    }

    // Ajuster l'offset en fonction de la vitesse
    hueOffset += map(rainbowSpeed, 0, 255, -5, 5); // Contrôle de la vitesse et de la direction (-5 à 5)

    // Afficher les LEDs
    poulpyShow();
    break;
}


case 3: // Mode scintillement aléatoire
{
    // Paramètres DMX
    uint8_t colorIndex = dmxChannels[1];     // Couleur des pixels
    uint8_t numPixels = dmxChannels[2]/10;     // Nombre de pixels allumés simultanément
    uint8_t flickerRate = dmxChannels[3];   // Fréquence de scintillement (pixels par seconde)

    static unsigned long lastUpdateTime = 0; // Temps de la dernière mise à jour
    static bool pixelStates[MAXLEDLENGTH] = {false}; // État de chaque pixel (allumé ou éteint)

    // Convertir l'index en couleur
    Color pixelColor = convertToColor(colorIndex);

    // Calculer l'intervalle de mise à jour en millisecondes
    unsigned long interval = 1000 / map(flickerRate, 0, 255, 1, 100); // Intervalle en fonction du taux de scintillement

    // Mettre à jour les pixels si l'intervalle est écoulé
    if (millis() - lastUpdateTime > interval) {
        lastUpdateTime = millis();

        // Désactiver un pixel aléatoire (si nécessaire)
        for (int i = 0; i < ledCount; i++) {
            if (pixelStates[i] && random(0, 100) < 50) { // 50% de probabilité de s'éteindre
                pixelStates[i] = false;
            }
        }

        // Activer de nouveaux pixels aléatoires
        int activePixels = 0;
        for (int i = 0; i < ledCount; i++) {
            if (pixelStates[i]) {
                activePixels++;
            }
        }

        while (activePixels < numPixels) {
            int randomPixel = random(0, ledCount);
            if (!pixelStates[randomPixel]) {
                pixelStates[randomPixel] = true;
                activePixels++;
            }
        }
    }

    // Appliquer les couleurs
    for (int i = 0; i < ledCount; i++) {
        if (pixelStates[i]) {
            leds[i].r = pixelColor.r;
            leds[i].g = pixelColor.g;
            leds[i].b = pixelColor.b;
        } else {
            leds[i].r = 0;
            leds[i].g = 0;
            leds[i].b = 0;
        }
    }

    // Afficher les LEDs
    poulpyShow();
    break;
}



case 4: // Mode scintillement progressif
{
    // Paramètres DMX
    uint8_t colorIndex = dmxChannels[1];     // Couleur des pixels
    uint8_t numPixels = dmxChannels[2] / 10; // Nombre de pixels allumés simultanément (ajusté)
    uint8_t flickerRate = dmxChannels[3]/2;   // Fréquence de scintillement (pixels par seconde)

    static unsigned long lastUpdateTime = 0; // Temps de la dernière mise à jour
    static float pixelIntensities[MAXLEDLENGTH] = {0.0}; // Intensité de chaque pixel (de 0.0 à 1.0)
    static int pixelTargets[MAXLEDLENGTH] = {0};         // État cible (1 = allumé, 0 = éteint)

    // Convertir l'index en couleur
    Color pixelColor = convertToColor(colorIndex);

    // Calculer l'intervalle de mise à jour en millisecondes
    unsigned long interval = 1000 / map(flickerRate, 0, 255, 1, 100); // Intervalle en fonction du taux de scintillement

    // Mettre à jour les pixels si l'intervalle est écoulé
    if (millis() - lastUpdateTime > interval) {
        lastUpdateTime = millis();

        // Désactiver un pixel aléatoire (si nécessaire)
        for (int i = 0; i < ledCount; i++) {
            if (pixelTargets[i] == 1 && random(0, 100) < 50) { // 50% de probabilité de passer à l'état "éteint"
                pixelTargets[i] = 0;
            }
        }

        // Activer de nouveaux pixels aléatoires
        int activePixels = 0;
        for (int i = 0; i < ledCount; i++) {
            if (pixelTargets[i] == 1) {
                activePixels++;
            }
        }

        while (activePixels < numPixels) {
            int randomPixel = random(0, ledCount);
            if (pixelTargets[randomPixel] == 0) {
                pixelTargets[randomPixel] = 1; // Passer à l'état "allumé"
                activePixels++;
            }
        }
    }

  for (int i = 0; i < ledCount; i++) {
    if (pixelTargets[i] == 1) {
        pixelIntensities[i] = min(pixelIntensities[i] + FADE_SPEED, 1.0); // Augmenter progressivement
    } else {
        pixelIntensities[i] = max(pixelIntensities[i] - FADE_SPEED, 0.0); // Diminuer progressivement
    }
}


    // Appliquer les couleurs en fonction des intensités
    for (int i = 0; i < ledCount; i++) {
        leds[i].r = pixelColor.r * pixelIntensities[i];
        leds[i].g = pixelColor.g * pixelIntensities[i];
        leds[i].b = pixelColor.b * pixelIntensities[i];
    }

    // Afficher les LEDs
    poulpyShow();
    break;
}


case 5: // Mode scintillement progressif par groupes
{
    // Paramètres DMX
    uint8_t colorIndex = dmxChannels[1];     // Couleur des pixels
    uint8_t numGroups = dmxChannels[2] / 10; // Nombre de groupes actifs simultanément (ajusté)
    uint8_t flickerRate = dmxChannels[3] / 2; // Fréquence de scintillement (groupes par seconde)

    static unsigned long lastUpdateTime = 0; // Temps de la dernière mise à jour
    static float groupIntensities[MAXLEDLENGTH] = {0.0}; // Intensité de chaque groupe (de 0.0 à 1.0)
    static int groupTargets[MAXLEDLENGTH] = {0};         // État cible (1 = allumé, 0 = éteint)
    static int groupLengths[MAXLEDLENGTH] = {0};         // Longueur des groupes

    // Convertir l'index en couleur
    Color pixelColor = convertToColor(colorIndex);

    // Calculer l'intervalle de mise à jour en millisecondes
    unsigned long interval = 1000 / map(flickerRate, 0, 255, 1, 100); // Intervalle en fonction du taux de scintillement

    // Mettre à jour les groupes si l'intervalle est écoulé
    if (millis() - lastUpdateTime > interval) {
        lastUpdateTime = millis();

        // Désactiver un groupe aléatoire (si nécessaire)
        for (int i = 0; i < ledCount; i++) {
            if (groupTargets[i] == 1 && random(0, 100) < 50) { // 50% de probabilité de passer à l'état "éteint"
                groupTargets[i] = 0;
            }
        }

        // Activer de nouveaux groupes aléatoires
        int activeGroups = 0;
        for (int i = 0; i < ledCount; i++) {
            if (groupTargets[i] == 1) {
                activeGroups++;
            }
        }

        while (activeGroups < numGroups) {
            int randomGroupStart = random(0, ledCount);
            int groupLength = random(1, 6); // Longueur aléatoire entre 1 et 5 pixels

            if (groupTargets[randomGroupStart] == 0) {
                groupTargets[randomGroupStart] = 1; // Passer à l'état "allumé"
                groupLengths[randomGroupStart] = groupLength; // Assigner une longueur aléatoire
                activeGroups++;
            }
        }
    }

    // Mettre à jour les intensités pour chaque groupe
    for (int i = 0; i < ledCount; i++) {
        if (groupTargets[i] == 1) {
            groupIntensities[i] = min(groupIntensities[i] + FADE_SPEED, 1.0); // Augmenter progressivement
        } else {
            groupIntensities[i] = max(groupIntensities[i] - FADE_SPEED, 0.0); // Diminuer progressivement
        }

        // Appliquer les couleurs pour les pixels dans chaque groupe
        for (int j = 0; j < groupLengths[i]; j++) {
            int pixelIndex = (i + j) % ledCount; // Gérer les débordements
            leds[pixelIndex].r = pixelColor.r * groupIntensities[i];
            leds[pixelIndex].g = pixelColor.g * groupIntensities[i];
            leds[pixelIndex].b = pixelColor.b * groupIntensities[i];
        }
    }

    // Afficher les LEDs
    poulpyShow();
    break;
}

   

  

  case 6: // Chaque groupe a une couleur unique pour tous ses pixels. Cette couleur est définie par trois canaux représentant ses valeurs RGB. 
         // Groupe 0 : RGB = 2 3 4, groupe 1 : RGB = 5 6 7, etc. 
    for (int j = 0; j < ledCount * 3; j += 3)
    {
      leds[j / 3].r = dmxChannels[ir];
      leds[j / 3].g = dmxChannels[ig];
      leds[j / 3].b = dmxChannels[ib];
    }
    break;

case 7: // "grésillement lumineux" général

if(dmxChannels[1] !=0)
   {
    // Couleur : on prend le triplet (9,10,11) => groupe 0
    tempr = dmxChannels[8];
    tempg = dmxChannels[9];
    tempb = dmxChannels[10];
  } else {
    // Couleur dépendant du groupe => 9+3*g, 10+3*g, 11+3*g
    tempr = dmxChannels[8  + 3 * setupTubeNumber];
    tempg = dmxChannels[9  + 3 * setupTubeNumber];
    tempb = dmxChannels[10  + 3 * setupTubeNumber];
  }
flickering(dmxChannels[1],dmxChannels[2],dmxChannels[3],dmxChannels[4],dmxChannels[5],dmxChannels[6],dmxChannels[7],tempr,tempg,tempb);
break;

case 8: // "grésillement lumineux" preset 1
flickering(0,0,183,110,93,25,61,255,68,0);
break;

case 9: // "grésillement lumineux" preset 2
flickering(0,0,147,29,100,63,6,0,0,255);
break;

case 10: // "grésillement lumineux" preset 3
flickering(0,0,147,29,100,63,6,255,255,255);
break;

case 11: // "grésillement lumineux" preset 3
flickering(0,0,255,255,29,31,6,255,255,255);
break;

  case 12: // Segment RGB : ch2–4 R,G,B ; ch5 longueur (0–255→1..ledCount) ; ch6 décalage origine (0–255→0..ledCount-1)
  {
    uint8_t rr = dmxChannels[1];
    uint8_t gg = dmxChannels[2];
    uint8_t bb = dmxChannels[3];
    uint8_t lenDm = dmxChannels[4];
    uint8_t offDm = dmxChannels[5];

    int start = map((int)offDm, 0, 255, 0, max(0, ledCount - 1));
    int segLen = map((int)lenDm, 0, 255, 1, ledCount);
    if (segLen < 1)
      segLen = 1;
    if (start < 0)
      start = 0;
    if (start >= ledCount)
      start = ledCount - 1;
    if (start + segLen > ledCount)
      segLen = ledCount - start;

    for (int j = 0; j < ledCount; j++) {
      leds[j].r = 0;
      leds[j].g = 0;
      leds[j].b = 0;
    }
    for (int k = 0; k < segLen; k++) {
      int idx = start + k;
      if (idx >= 0 && idx < ledCount) {
        leds[idx].r = rr;
        leds[idx].g = gg;
        leds[idx].b = bb;
      }
    }
    break;
  }


  case 255: // affichage du numéro de groupe
    for (int j = 0; j < setupTubeNumber; j++)
    {
      leds[10 * j].r = 255;
      leds[10 * j].g = 0;
      leds[10 * j].b = 0;
    }
    break;

 
  default:
    // Mode inconnu : blackout (FastLED.clear déjà appelé)
    break;

  }

  poulpyShow();
  delay(1);
}

// ----- button 1 callback functions
void click1() // en mode SETUP, chaque clic simple sur le bouton incrémente le numéro de groupe
{
  if(!BUTTON_PRESENT) return;
  if (etat == RUNNING)
    return; // en mode RUNNING, on ignore cette action
  setupTubeNumber = (setupTubeNumber + 1) % NBGROUPS;
}

void longPressStart1() // un clic long, permet de passer de RUNNING à SETUP et inversement (à la sortie du mode SETUP, on enregistre les données en mémoire persistante)
{
  if(!BUTTON_PRESENT) return;

  Serial.print("longpress | etat = ");
  Serial.println((etat ? "RUNNING" : "SETUP"));

  if (etat == SETUP) // avant de sortir du SETUP, on enregistre les données en mémoire persistante
  {
    EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS, setupAddress & 0xFF);
    EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS + 1, (setupAddress >> 8) & 0xFF);
    EEPROM.write(EEPROM_ADDR_SETUP_MODE, setupMode);
    EEPROM.write(EEPROM_ADDR_SETUP_TUBE, setupTubeNumber);
    Serial.print("Commit =  ");
    Serial.println(EEPROM.commit());
  }

  etat = !etat;
}

void setup()
{
  bootTime = millis();  // Enregistrer le temps de démarrage pour calcul uptime
  randomHueOffset = rand()%256;
  Serial.begin(115200);
  Serial.println("");
  Serial.printf("Version %s (int=%d, compiled=%s)\n", VERSION, VERSION_INT, VERSION_TIMESTAMP);

#if defined(ESP8266)
  pinMode(BUTTONGROUNDPIN, OUTPUT); 
  digitalWrite(BUTTONGROUNDPIN, LOW); // on utilise BUTTONGROUNDPIN comme GND pour le bouton 1
#endif

  FastLED.addLeds<WS2812, POULPY_LED_PIN, GRB>(leds, MAXLEDLENGTH); // Création d'un objet représentant le ledstrip pour FastLED // GRB ordering is typical

  // Initialiser EEPROM avant tout
  EEPROM.begin(EEPROM_SIZE);
  
  // Vérifier et exécuter une mise à jour OTA en attente AVANT d'initialiser ESP-NOW
  executeOTAUpdate();
  
  // Si on arrive ici, pas d'OTA en attente, continuer normalement
  WiFi.disconnect();
#if defined(ESP8266)
  ESP.eraseConfig(); // !!! COMMANDES IMPORTANTES !!! ESP_NOW peut parfois ne pas fonctionner si on n'exécute pas pas ces deux commandes. Je ne suis pas sûr que ce soit écrit dans la doc. C'est peut-être même un petit bug. Bref, il faut le savoir :)
#else
  WiFi.persistent(false);
#endif

  // Wifi STA Mode
  WiFi.mode(WIFI_STA);
  // Get Mac Address
  Serial.print("Mac Address: ");
  Serial.print(WiFi.macAddress());
  Serial.println("\nESP-Now Receiver");
  
  // ========== GESTION CANAL ESP-NOW ==========
  // Utiliser directement le canal sauvegardé en EEPROM (défini par diffusion depuis l'émetteur)
  uint8_t savedChannel = loadDetectedChannel();
  
  if (savedChannel >= 1 && savedChannel <= 13) {
    // Canal valide en EEPROM, l'utiliser directement
    detectedChannel = savedChannel;
    Serial.printf("Canal chargé depuis EEPROM: %d\n", detectedChannel);
    wifi_set_channel(detectedChannel);
  } else {
    // Aucun canal valide en EEPROM, faire un scan rapide pour trouver l'émetteur
    // (nécessaire pour la première utilisation ou si l'EEPROM a été effacée)
    Serial.println("ATTENTION: Aucun canal valide en EEPROM, scan rapide pour trouver l'émetteur...");
    detectedChannel = scanForChannel();
    
    if (detectedChannel == 0) {
      // Scan échoué, utiliser le canal 1 par défaut
      Serial.println("Scan échoué, utilisation du canal 1 par défaut");
      detectedChannel = 1;
      wifi_set_channel(1);
      saveDetectedChannel(1); // Sauvegarder le canal par défaut
    } else {
      // Scan réussi, sauvegarder le canal trouvé
      saveDetectedChannel(detectedChannel);
    }
  }
  
  // Initialiser le timestamp pour la surveillance
  lastPacketTime = millis();
  DEBUG_PRINTF("Canal WiFi configuré: %d\n", wifi_get_channel());
  // ========== FIN GESTION CANAL ESP-NOW ==========

  // Initializing the ESP-NOW
  if (esp_now_init() != 0)
  {
    Serial.println("Problem during ESP-NOW init");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // Note: Le mode promiscuous ne peut pas être utilisé avec ESP-NOW sur ESP8266
  // (ils entrent en conflit). Le RSSI sera approximatif via WiFi.RSSI().

  // ========== CONFIGURATION ESP-NOW POUR LES ENVOIS ==========
  // Définir le rôle ESP-NOW (nécessaire avant d'ajouter des peers)
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  
  // Ajouter l'adresse broadcast comme peer pour pouvoir envoyer des réponses
  // API ESP8266 (espnow.h) : esp_now_add_peer(mac, role, channel, key, key_len)
  int result = esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);
  if (result != 0) {
    Serial.printf("Erreur lors de l'ajout du peer broadcast: %d\n", result);
  } else {
    Serial.println("Adresse broadcast ajoutée comme peer ESP-NOW");
    DEBUG_PRINTF("Peer broadcast ajouté sur canal: %d\n", detectedChannel);
  }
  
  // Si on a détecté une MAC d'émetteur pendant le scan, l'ajouter comme peer spécifique
  // Note: channelDetected est réinitialisé après le scan, donc on vérifie detectedChannel > 0
  if (detectedChannel > 0 && detectedChannel <= 13) {
    // Vérifier si senderMacFromScan contient une adresse valide (pas tout à 0xFF ou 0x00)
    bool validMac = false;
    for (int i = 0; i < 6; i++) {
      if (senderMacFromScan[i] != 0x00 && senderMacFromScan[i] != 0xFF) {
        validMac = true;
        break;
      }
    }
    
    if (validMac) {
      int peerResult = esp_now_add_peer(senderMacFromScan, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);
      if (peerResult == 0) {
        DEBUG_PRINTF("Peer émetteur ajouté sur canal: %d\n", detectedChannel);
      } else {
        DEBUG_PRINTF("Erreur ajout peer émetteur: %d\n", peerResult);
      }
    }
  }

  // link the button 1 functions.
  button1.setDebounceMs(50);
  button1.setPressMs(1000); 
  button1.attachClick(click1);
  button1.attachLongPressStart(longPressStart1);

  // EEPROM déjà initialisé plus haut
  setupAddress = EEPROM.read(EEPROM_ADDR_SETUP_ADDRESS) | (EEPROM.read(EEPROM_ADDR_SETUP_ADDRESS + 1) << 8);
  setupMode = EEPROM.read(EEPROM_ADDR_SETUP_MODE);
  setupTubeNumber = EEPROM.read(EEPROM_ADDR_SETUP_TUBE);
  
  // Charger le nombre de LEDs (uint16_t = 2 bytes)
  uint16_t savedLedCount = EEPROM.read(EEPROM_ADDR_LED_COUNT) | (EEPROM.read(EEPROM_ADDR_LED_COUNT + 1) << 8);
  if (savedLedCount > 0 && savedLedCount <= MAXLEDLENGTH) {
    ledCount = savedLedCount;
    Serial.printf("Nombre de LEDs chargé depuis EEPROM: %d\n", ledCount);
  } else {
    ledCount = 144;  // Valeur par défaut (ruban standard)
    Serial.printf("Nombre de LEDs par défaut: %d\n", ledCount);
  }

  powerBudgetLoad();
  
  if ((setupTubeNumber < 0) || (setupTubeNumber > 32))
    setupTubeNumber = 0;
  if (EEPROM.read(EEPROM_ADDR_MULTIFIX_INIT) != MULTIFIX_INIT_MAGIC) {
    int defAddr = 1 + MULTIFIXTURE_SLOT * setupTubeNumber;
    if (defAddr > 512 - MULTIFIXTURE_SLOT + 1) defAddr = 1;
    setupAddress = defAddr;
    EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS, setupAddress & 0xFF);
    EEPROM.write(EEPROM_ADDR_SETUP_ADDRESS + 1, (setupAddress >> 8) & 0xFF);
    EEPROM.write(EEPROM_ADDR_MULTIFIX_INIT, MULTIFIX_INIT_MAGIC);
    EEPROM.commit();
    Serial.printf("Adresse multifixure par défaut: %d (tube %d)\n", setupAddress, setupTubeNumber);
  }
  if ((setupAddress < 1) || (setupAddress > 512 - MULTIFIXTURE_SLOT + 1))
    setupAddress = 1;
  if ((setupMode < 1) || (setupMode > 255))
    setupMode = 1;

  // EEPROM.end();

  Serial.print(" ");
  Serial.print("Paramètres récupérés en EEPROM : Addresse = ");
  Serial.print(setupAddress);
  Serial.print(" | Mode = ");
  Serial.print(setupMode);
  Serial.print(" | Tube Group = ");
  Serial.println(setupTubeNumber);
}

void loop() 
{
  if(BUTTON_PRESENT) button1.tick(); // fonction vérifiant l'état du bouton

  // ========== AFFICHAGE PÉRIODIQUE DES STATISTIQUES ==========
  unsigned long currentTime = millis();
  if (currentTime - lastStatsTime >= 1000) {
    lastStatsTime = currentTime;
    
    // Afficher les stats sur le serial uniquement si statsSerialEnabled est activé
    if (statsSerialEnabled) {
      DEBUG_PRINTLN("========== STATISTIQUES ESP-NOW ==========");
      DEBUG_PRINTF("Trames totales: %lu | DMX: %lu | OTA: %lu | MAC: %lu | SETUP: %lu | Invalides: %lu\n",
                    statsTotalPackets, statsDMXPackets, statsOTAPackets, 
                    statsMACPackets, statsSETUPPackets, statsInvalidPackets);
      DEBUG_PRINTF("RSSI: %d dBm | Canal WiFi: %d\n", lastRSSI, wifi_get_channel());
      DEBUG_PRINTLN("==========================================");
    }
    
    // Réinitialiser les compteurs pour la prochaine seconde (optionnel)
    // Si vous préférez garder un total cumulé, commentez ces lignes :
    // statsTotalPackets = 0;
    // statsDMXPackets = 0;
    // statsOTAPackets = 0;
    // statsMACPackets = 0;
    // statsSETUPPackets = 0;
    // statsInvalidPackets = 0;
  }
  // ========== FIN AFFICHAGE STATISTIQUES ==========

  // ========== ENVOI STATS PÉRIODIQUES (si activé) ==========
  if (statsStreamActive && (currentTime - lastStatsStreamTime >= 1000)) {
    sendStatsResponse();
    lastStatsStreamTime = currentTime;
  }
  // ========== FIN ENVOI STATS PÉRIODIQUES ==========

  // ========== GESTION MODE IDENTIFICATION ==========
  handleIdentifyMode();
  // ========== FIN GESTION IDENTIFICATION ==========

  // ========== SURVEILLANCE TIMEOUT ==========
  // Note: Le scan automatique a été supprimé. Le canal est maintenant géré uniquement
  // par diffusion depuis l'émetteur via SET_CHANNEL_CMD.
  // On garde juste la mise à jour de lastPacketTime pour les statistiques.
  // ========== FIN SURVEILLANCE TIMEOUT ==========

  // ========== PRIORITÉ AU SETUP À DISTANCE ==========
  if (remoteSetupMode) {
    handleRemoteSetup(); // Gestion du setup à distance
    delay(1); // Petit délai pour éviter la surcharge
    return; // Sortir immédiatement, ignorer le reste de la loop
  }
  // ========== FIN PRIORITÉ SETUP À DISTANCE ==========

  // ========== MODE CONFIGURATION À DISTANCE ==========
  if (remoteConfigActive) {
    displayRemoteConfigPattern(); // Affichage spécial clignotant
    delay(1); // Petit délai pour éviter la surcharge
    return; // Sortir immédiatement, ignorer le DMX
  }
  // ========== FIN MODE CONFIGURATION À DISTANCE ==========

  if (etat == RUNNING)
  {
    if (demoAnimPendingIndex != 255) {
      uint8_t ix = demoAnimPendingIndex;
      demoAnimPendingIndex = 255;
      demoAnimFlashColor =
          CRGB(DEMO_ANIM_COLORS[ix][0], DEMO_ANIM_COLORS[ix][1], DEMO_ANIM_COLORS[ix][2]);
      demoAnimPhase = DEMO_ANIM_FLASH_HOLD;
      demoAnimPhaseStartMs = millis();
    }

    if (demoAnimPhase != DEMO_ANIM_IDLE)
      updateDemoAnimation();
    else
      DMX2LEDSTRIP(); // on met à jour l'affichage du ledstrip
  }
  else // etat==SETUP -> on fait clignoter un nombre de LEDs correspondant au groupe du tube
  {
    FastLED.clear();
    if (setupTubeNumber == 0)
      leds[0].r = 250; // pour le groupe 0, on fait clignoter la première led en rouge
    else
    {
      for (int j = 1; j <= NBGROUPS * setupTubeNumber; j += NBGROUPS) // pour un groupe n (différent de 0), on fait clignotter n leds en vert
      {
        leds[j].r = 0;
        leds[j].g = 250;
        leds[j].b = 0;
      }
    }
    poulpyShow(); // les cinq lignes suivantes produisent un clignottement (200ms)
    delay(100);
    FastLED.clear();
    poulpyShow();
    delay(100);
  }
}
