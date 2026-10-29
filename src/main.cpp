// PoulpyTube — récepteur ESP-NOW pour ruban LED (Wemos D1 mini, ou ESP32 avec POULPY_ESP32_TUBE).
//
// Le tube reçoit un univers DMX de 512 canaux (4 blocs de 128) et des commandes.
// Deux façons de lire l'univers :
//   - flux « multifixture » (Pollux via PoulpyNode / PoulpyLink) : slot de 10 canaux
//     à l'adresse du tube (multifixure.cpp) ;
//   - anciens modes : canal 1 = numéro de mode (legacy.cpp, led_modes.cpp).
//
// Règles : la réception radio ne fait que copier le bloc DMX ou mettre la commande
// en file ; tout le reste se passe dans loop(), sans delay(). Seuls le démarrage
// (canal inconnu, mise à jour OTA) bloque.
// Architecture cible : docs/ARCHITECTURE.md.

#include "build_version.h"

#include <Arduino.h>
#include <FastLED.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <espnow.h>
extern "C" {
#include "user_interface.h"
}
#define POULPY_HTTPUPDATE ESPhttpUpdate
#define POULPY_LED_PIN D2
#else
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "esp32_compat.h"
#define POULPY_HTTPUPDATE httpUpdate
#define POULPY_LED_PIN 16  // câblage du module ESP32 (WLED l'utilisait)
#endif

#include "autolook.h"
#include "config.h"
#include "gateway.h"
#include "legacy.h"
#include "led_modes.h"
#include "multifixure.h"
#include "power_budget.h"
#include "protocol.h"
#include "tube.h"

static const char* const LEGACY_OTA_URL = "http://poulpylights.gaetanstreel.com/receiver/firmware.bin";
static const uint16_t FRAME_MS = 10;
static const unsigned long IDENTIFY_MS = 3000;

// ---------- État partagé avec les modules de rendu ----------

CRGB leds[MAXLEDLENGTH];
uint8_t dmxChannels[512];
uint16_t ledCount = DEFAULT_LED_COUNT;
int setupTubeNumber = 0;  // groupe 0..9
uint8_t hueOffset = 0;

int setupAddress = 1;  // 0 = suit le groupe
TubeSettings settings;
static volatile bool multifixtureEnabled = false;
static volatile unsigned long lastLiveMs = 0;  // dernier bloc DMX reçu, 0 = jamais
static bool settingsDirty = false;
static unsigned long settingsDirtyAt = 0;
static volatile unsigned long lastMultifixureMs = 0;
static volatile bool remoteConfigActive = false;  // sélection depuis Pollux : motif rouge/blanc, DMX ignoré

uint8_t myMac[6];
static uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t senderMacAddress[6];
static bool hasSenderAddress = false;
uint8_t detectedChannel = 0;
static uint8_t maxBrightness = 255;
static unsigned long bootTime = 0;

static unsigned long remoteConfigSince = 0;
static bool identifyActive = false;
static unsigned long identifySince = 0;

static bool statsStreamActive = false;
static bool statsSerialEnabled = false;
static unsigned long lastStatsStreamTime = 0;
static volatile uint32_t statsStreamPacketCount = 0;

static bool restartPending = false;
static unsigned long restartAt = 0;

static unsigned long lastFrameMs = 0;
static uint16_t framePeriodMs = FRAME_MS;

// Scan de canal au démarrage
static volatile bool channelDetected = false;
static uint8_t senderMacFromScan[6];

void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len);

static void scheduleRestart(unsigned long inMs) {
  restartPending = true;
  restartAt = millis() + inMs;
}

static bool isCommandForMe(const uint8_t* data) {
  return memcmp(&data[CMD_TARGET_MAC], myMac, 6) == 0;
}

static void fillFirst(int n, CRGB c) {
  for (int i = 0; i < min(n, (int)ledCount); i++) leds[i] = c;
}

// ---------- File des commandes (réception → loop) ----------

struct RxCommand {
  uint8_t mac[6];
  struct_dmx_packet packet;
};

static const uint8_t CMD_QUEUE_SIZE = 12;  // une rediffusion de passerelle = ~12 paquets
static RxCommand cmdQueue[CMD_QUEUE_SIZE];
static volatile uint8_t cmdHead = 0;  // écrit par la réception
static volatile uint8_t cmdTail = 0;  // écrit par loop()

static void enqueueCommand(const uint8_t* mac, const struct_dmx_packet* p) {
  uint8_t next = (cmdHead + 1) % CMD_QUEUE_SIZE;
  if (next == cmdTail) return;  // pleine : la commande sera renvoyée par l'émetteur
  memcpy(cmdQueue[cmdHead].mac, mac, 6);
  memcpy(&cmdQueue[cmdHead].packet, p, sizeof(*p));
  __sync_synchronize();
  cmdHead = next;
}

// ---------- Réception ESP-NOW ----------

void OnDataRecv(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
  if (len != sizeof(struct_dmx_packet)) return;
  const struct_dmx_packet* p = (const struct_dmx_packet*)incomingData;
  if (p->data[0] != SIGNATURE_A || p->data[1] != SIGNATURE_B || p->data[2] != SIGNATURE_C) return;

  if (isCommandCode(p->data[3])) {
    enqueueCommand(mac, p);
    return;
  }

  if (p->blockNumber >= 4) return;
  statsStreamPacketCount++;
  if (remoteConfigActive) return;

  memcpy(&dmxChannels[p->blockNumber * 128], p->dmxvalues, 128);
  lastLiveMs = millis() | 1;
  multifixtureEnabled = p->data[STREAM_KIND] == STREAM_MULTIFIXTURE
                     && p->data[STREAM_MULTIFIXTURE_MARK] == STREAM_MULTIFIXTURE_MAGIC;
  if (multifixtureEnabled) lastMultifixureMs = millis();
}

// Pendant le scan de démarrage : compter les paquets PoulpyTube, rien d'autre.
static void OnScanReceive(uint8_t* mac, uint8_t* incomingData, uint8_t len) {
  if (len != sizeof(struct_dmx_packet)) return;
  const uint8_t* d = incomingData + 1 + 128;
  if (d[0] == SIGNATURE_A && d[1] == SIGNATURE_B && d[2] == SIGNATURE_C) {
    channelDetected = true;
    memcpy(senderMacFromScan, mac, 6);
  }
}

// ---------- Envoi des réponses ----------

static void prepareResponsePacket(struct_dmx_packet* packet, uint8_t cmdType) {
  memset(packet, 0, sizeof(*packet));
  packet->blockNumber = CMD_BLOCK;
  packet->data[0] = SIGNATURE_A;
  packet->data[1] = SIGNATURE_B;
  packet->data[2] = SIGNATURE_C;
  packet->data[3] = cmdType;
  memcpy(&packet->data[CMD_TARGET_MAC], myMac, 6);
}

static void sendResponsePacket(struct_dmx_packet* packet) {
  uint8_t* target = broadcastAddress;
  if (hasSenderAddress) {
    if (esp_now_is_peer_exist(senderMacAddress)
        || esp_now_add_peer(senderMacAddress, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0) == 0)
      target = senderMacAddress;
  }
  esp_now_send(target, (uint8_t*)packet, sizeof(struct_dmx_packet));
}

static void put16(uint8_t* d, uint16_t v) {
  d[0] = v & 0xFF;
  d[1] = (v >> 8) & 0xFF;
}

static void put32(uint8_t* d, uint32_t v) {
  for (int i = 0; i < 4; i++) d[i] = (v >> (8 * i)) & 0xFF;
}

static void sendMACResponse() {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_MAC_CMD);
  r.data[10] = setupTubeNumber;
  r.data[11] = VERSION_INT;
  sendResponsePacket(&r);
}

static void sendInfoResponse() {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_INFO_CMD);
  r.data[10] = setupTubeNumber;
  r.data[11] = VERSION_INT;
  r.data[12] = detectedChannel;
  put16(&r.data[13], ledCount);
  r.data[15] = maxBrightness;
  r.data[16] = 0;  // RSSI : pas mesurable en ESP-NOW sur ESP8266
  put32(&r.data[17], (millis() - bootTime) / 1000);
  put32(&r.data[21], ESP.getFreeHeap());
  r.data[25] = remoteConfigActive ? 1 : (identifyActive ? 3 : 0);  // 0 marche, 1 sélection, 3 identification
  r.data[26] = statsStreamActive ? 1 : 0;
  r.data[27] = statsSerialEnabled ? 1 : 0;
  uint8_t vlen = min((size_t)24, strlen(VERSION));
  r.data[28] = vlen;
  memcpy(&r.data[29], VERSION, vlen);
  put16(&r.data[53], setupAddress);
  r.data[55] = multifixtureEnabled ? 1 : 0;
  put32(&r.data[56], powerBudgetEnergyMwh());
  put32(&r.data[60], powerBudgetInstantMw());
  put16(&r.data[64], powerBudgetLimitW());
  sendResponsePacket(&r);
}

static void sendDmxAddressResponse() {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_DMX_ADDRESS_CMD);
  put16(&r.data[10], setupAddress);
  r.data[12] = multifixtureEnabled ? 1 : 0;
  sendResponsePacket(&r);
}

static void sendChannelResponse() {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_CHANNEL_CMD);
  r.data[10] = detectedChannel;
  r.data[11] = wifi_get_channel();
  sendResponsePacket(&r);
}

static void sendStatsResponse() {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_STATS_CMD);
  put32(&r.data[10], statsStreamPacketCount);
  r.data[14] = 0;
  r.data[15] = detectedChannel;
  put32(&r.data[16], (millis() - bootTime) / 1000);
  r.data[20] = setupTubeNumber;
  sendResponsePacket(&r);
  statsStreamPacketCount = 0;
}

static void sendAckResponse(uint8_t originalCmd, uint8_t status) {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_ACK_CMD);
  r.data[10] = originalCmd;
  r.data[11] = status;
  sendResponsePacket(&r);
}

static void sendPongResponse() {
  struct_dmx_packet r;
  prepareResponsePacket(&r, RESPONSE_PONG_CMD);
  r.data[10] = 0;
  r.data[11] = detectedChannel;
  put32(&r.data[12], millis());
  sendResponsePacket(&r);
}

// ---------- Commandes ----------

// SSID et mot de passe dans data[], URL optionnelle dans dmxvalues[].
// Enregistré en EEPROM ; la mise à jour se fait au redémarrage.
static void handleOTAUpdate(const uint8_t* d, const uint8_t* dmxValues) {
  uint8_t ssidLen = d[4];
  if (ssidLen == 0 || ssidLen > 32) return;
  uint8_t passLen = d[5 + ssidLen];
  if (passLen > 63) return;

  char ssid[33], password[64], url[141] = {0};
  memcpy(ssid, &d[5], ssidLen);
  ssid[ssidLen] = '\0';
  memcpy(password, &d[6 + ssidLen], passLen);
  password[passLen] = '\0';
  if (dmxValues[0] != 0) strncpy(url, (const char*)dmxValues, 140);

  Serial.printf("OTA programmée (Wi-Fi %s)\n", ssid);
  configSaveOta(ssid, password, url[0] ? url : nullptr);
  scheduleRestart(300);
}

static void handleNewCommand(uint8_t cmd, const uint8_t* d) {
  switch (cmd) {
    case GET_INFO_CMD:
      sendInfoResponse();
      break;

    case GET_CHANNEL_CMD:
      sendChannelResponse();
      break;

    case SET_CHANNEL_CMD: {
      uint8_t ch = d[10];
      if (ch < 1 || ch > 13) {
        sendAckResponse(cmd, ACK_INVALID);
      } else if (ch == detectedChannel) {
        sendAckResponse(cmd, ACK_OK);
      } else {
        configSaveChannel(ch);
        sendAckResponse(cmd, ACK_OK);
        scheduleRestart(700);  // laisser partir l'ACK
      }
      break;
    }

    case SET_DMX_ADDRESS_CMD: {
      uint16_t addr = d[10] | (d[11] << 8);
      if (addr <= 512 - MULTIFIXTURE_SLOT + 1) {  // 0 = suit le groupe
        setupAddress = addr;
        configSaveAddress(addr);
        sendAckResponse(cmd, ACK_OK);
      } else {
        sendAckResponse(cmd, ACK_INVALID);
      }
      break;
    }

    case SET_MULTIFIXTURE_CMD:
      multifixtureEnabled = d[10] != 0;
      if (multifixtureEnabled) lastMultifixureMs = millis();
      sendAckResponse(cmd, ACK_OK);
      break;

    case GET_DMX_ADDRESS_CMD:
      sendDmxAddressResponse();
      break;

    case SET_LED_COUNT_CMD: {
      uint16_t n = d[10] | (d[11] << 8);
      if (n > 0 && n <= MAXLEDLENGTH) {
        configSaveLedCount(n);
        sendAckResponse(cmd, ACK_OK);
        scheduleRestart(500);  // FastLED est déclaré avec le nombre de LEDs
      } else {
        sendAckResponse(cmd, ACK_INVALID);
      }
      break;
    }

    case START_STATS_CMD:
      statsStreamActive = true;
      statsStreamPacketCount = 0;
      lastStatsStreamTime = millis();
      sendAckResponse(cmd, ACK_OK);
      break;

    case STOP_STATS_CMD:
      statsStreamActive = false;
      sendAckResponse(cmd, ACK_OK);
      break;

    case SET_BRIGHTNESS_CMD:
      maxBrightness = d[10];
      FastLED.setBrightness(maxBrightness);
      sendAckResponse(cmd, ACK_OK);
      break;

    case RESTART_SINGLE_CMD:
      sendAckResponse(cmd, ACK_OK);
      scheduleRestart(500);
      break;

    case SAVE_CONFIG_CMD:
      configSaveAll(setupTubeNumber, detectedChannel, ledCount);
      sendAckResponse(cmd, ACK_OK);
      break;

    case IDENTIFY_CMD:
      identifyActive = true;
      identifySince = millis();
      sendAckResponse(cmd, ACK_OK);
      break;

    case PING_CMD:
      sendPongResponse();
      break;

    case SET_STATS_SERIAL_CMD:
      statsSerialEnabled = d[10] != 0;
      sendAckResponse(cmd, ACK_OK);
      break;

    case RESET_ENERGY_CMD:
      powerBudgetReset();
      sendAckResponse(cmd, ACK_OK);
      break;

    case SET_POWER_LIMIT_CMD: {
      uint16_t watts = d[10] | (d[11] << 8);
      if (watts <= 200) {
        powerBudgetSetLimitW(watts);
        sendAckResponse(cmd, ACK_OK);
      } else {
        sendAckResponse(cmd, ACK_INVALID);
      }
      break;
    }

    default:
      sendAckResponse(cmd, ACK_UNKNOWN);
      break;
  }
}

static void processCommand(const uint8_t* mac, const struct_dmx_packet& p) {
  const uint8_t* d = p.data;
  const uint8_t cmd = d[3];

  switch (cmd) {
    case OTA_UPDATE_CMD:
      handleOTAUpdate(d, p.dmxvalues);
      return;

    case REQUEST_MAC_CMD:
      memcpy(senderMacAddress, mac, 6);
      hasSenderAddress = true;
      sendMACResponse();
      return;

    case ENTER_REMOTE_SETUP_CMD:
      if (!isCommandForMe(d)) return;
      remoteConfigActive = true;
      remoteConfigSince = millis();
      return;

    case EXIT_REMOTE_SETUP_CMD:
      if (!isCommandForMe(d)) return;
      remoteConfigActive = false;
      configSaveGroup(setupTubeNumber);
      return;

    case SET_GROUP_NUMBER_CMD:
      if (!isCommandForMe(d)) return;
      if (d[10] < GROUP_COUNT) {
        setupTubeNumber = d[10];
        configSaveGroup(setupTubeNumber);
      }
      return;

    case PANIC_RESTART_CMD:
      scheduleRestart(100);
      return;

    case LOOK_CMD:
      gatewayOnLook(d);
      return;

    case GATEWAY_BEACON_CMD:
      gatewayOnBeacon(d);
      return;

    case SET_ROLE_CMD:
      gatewayOnSetRole(d);
      return;

    case TUBE_STATUS_CMD:
      gatewayOnStatus(d);
      return;

    case WIFI_CONFIG_CMD:
      gatewayOnWifiConfig(d, p.dmxvalues);
      return;

    case RESPONSE_MAC_CMD:      // réponse d'un autre tube
    case ANIMATION_TRIGGER_CMD:  // plus utilisé
      return;
  }

  // 0xE0-0xEF, 0xF0, 0xF1 : répondre à l'émetteur. SET_CHANNEL est diffusé à tous.
  memcpy(senderMacAddress, mac, 6);
  hasSenderAddress = true;
  if (cmd == SET_CHANNEL_CMD || isCommandForMe(d)) handleNewCommand(cmd, d);
}

static void processCommands() {
  while (cmdTail != cmdHead) {
    __sync_synchronize();
    RxCommand& c = cmdQueue[cmdTail];
    processCommand(c.mac, c.packet);
    cmdTail = (cmdTail + 1) % CMD_QUEUE_SIZE;
  }
}

// ---------- Rendu ----------

// Sélection depuis Pollux : bandes de 10 LEDs blanc / rouge, clignotant à 1 Hz.
static void renderRemoteConfig(unsigned long now) {
  if (((now - remoteConfigSince) / 500) % 2 == 0) return;
  for (int i = 0; i < ledCount; i++) leds[i] = (i / 10) % 2 == 0 ? CRGB::White : CRGB::Red;
}

static void renderIdentify(unsigned long now) {
  if ((now - identifySince) / 200 % 2 == 0) fill_solid(leds, ledCount, CRGB::White);
}

static int effectiveAddress() {
  return setupAddress ? setupAddress : 1 + 10 * setupTubeNumber;
}

// Retourne la période d'image voulue par le mode affiché.
static uint16_t renderLive() {
  if (multifixtureEnabled) {
    renderMultifixture(dmxChannels, effectiveAddress(), ledCount);
    return FRAME_MS;
  }
  uint8_t mode = dmxChannels[0];
  if (renderExtendedLedMode(mode)) return FRAME_MS;
  return renderLegacyMode(mode);
}

// Le live garde la main jusqu'au délai de retour après son dernier paquet ;
// ensuite la dernière image s'éteint, puis le look autonome monte.
static const unsigned long RETURN_FADE_MS = 500;

static uint16_t renderFrame(unsigned long now) {
  const unsigned long last = lastLiveMs;
  const uint8_t seed = (uint8_t)(effectiveAddress() * 16);
  if (last) {
    const unsigned long hold = settings.returnDelayS * 1000UL;
    const unsigned long age = now - last;
    if (age < hold) return renderLive();
    if (age < hold + RETURN_FADE_MS) {
      renderLive();
      nscale8(leds, ledCount, 255 - (age - hold) * 255 / RETURN_FADE_MS);
      return FRAME_MS;
    }
    if (age < hold + 2 * RETURN_FADE_MS) {
      autoLookRender(now, ledCount, seed);
      nscale8(leds, ledCount, (age - hold - RETURN_FADE_MS) * 255 / RETURN_FADE_MS);
      return FRAME_MS;
    }
    noInterrupts();
    if (lastLiveMs == last) {
      lastLiveMs = 0;
      multifixtureEnabled = false;
    }
    interrupts();
  }
  autoLookRender(now, ledCount, seed);
  return FRAME_MS;
}

// ---------- Ce que la passerelle emprunte au tube (tube.h) ----------

void tubeProcessPacket(const struct_dmx_packet& p) {
  processCommand(myMac, p);
}

void tubeBroadcast(const struct_dmx_packet& p) {
  esp_now_send(broadcastAddress, (uint8_t*)&p, sizeof(struct_dmx_packet));
}

bool tubeIsLive() {
  const unsigned long last = lastLiveMs;
  return last && millis() - last < settings.returnDelayS * 1000UL;
}

void tubeSaveSettingsSoon() {
  settingsDirty = true;
  settingsDirtyAt = millis();
}

void tubeRestart(unsigned long inMs) {
  scheduleRestart(inMs);
}

// ---------- Démarrage : canal et mise à jour ----------

static int scanWiFiForChannel(const char* ssid) {
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; i++)
    if (WiFi.SSID(i) == ssid) return WiFi.channel(i);
  return -1;
}

static void startEspNow(uint8_t channel) {
  esp_now_deinit();
  wifi_set_channel(channel);
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW : échec d'initialisation");
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, channel, NULL, 0);
}

// Premier démarrage sans canal enregistré : écoute 200 ms par canal, garde le plus actif.
static uint8_t scanForChannel() {
  Serial.println("Recherche du canal ESP-NOW...");
  uint16_t best = 0;
  uint8_t bestChannel = 0;

  for (uint8_t ch = 1; ch <= 13; ch++) {
    FastLED.clear();
    fillFirst(ch, CRGB::Blue);
    poulpyShow();

    esp_now_deinit();
    wifi_set_channel(ch);
    delay(200);
    if (esp_now_init() != 0) continue;
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(OnScanReceive);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, ch, NULL, 0);

    uint16_t count = 0;
    unsigned long start = millis();
    while (millis() - start < 200) {
      yield();
      if (channelDetected) {
        count++;
        channelDetected = false;
      }
    }
    Serial.printf("Canal %d : %d paquets\n", ch, count);
    if (count > best) {
      best = count;
      bestChannel = ch;
    }
    esp_now_deinit();
    delay(50);
  }

  if (best < 10) return 0;
  for (int i = 0; i < 3; i++) {
    FastLED.clear();
    fillFirst(10, CRGB::Green);
    poulpyShow();
    delay(100);
    FastLED.clear();
    poulpyShow();
    delay(100);
  }
  return bestChannel;
}

static void showAll(CRGB c, int n) {
  FastLED.clear();
  fillFirst(n, c);
  poulpyShow();
}

static void blink(CRGB c, int n, int times, int ms) {
  for (int i = 0; i < times; i++) {
    showAll(c, n);
    delay(ms);
    FastLED.clear();
    poulpyShow();
    delay(ms);
  }
}

// Mise à jour programmée par OTA_UPDATE_CMD : rejoint le Wi-Fi, télécharge, redémarre.
static void executeOTAUpdate() {
  char ssid[33], password[64], url[141];
  if (!configLoadOta(ssid, password, url)) return;
  configClearOta();

  // Le Wi-Fi envoyé par Pollux sert aussi de Wi-Fi maison à la passerelle, s'il n'y en a pas.
  if (!settings.wifiSsid[0] && ssid[0]) {
    strncpy(settings.wifiSsid, ssid, sizeof(settings.wifiSsid) - 1);
    strncpy(settings.wifiPass, password, sizeof(settings.wifiPass) - 1);
    configSaveSettings(settings);
  }

  const char* firmwareUrl = url[0] ? url : LEGACY_OTA_URL;
  Serial.printf("Mise à jour : Wi-Fi %s, %s\n", ssid, firmwareUrl);
  blink(CRGB::Blue, 10, 5, 200);

  WiFi.mode(WIFI_STA);
  int ch = scanWiFiForChannel(ssid);
  if (ch > 0) wifi_set_channel(ch);
  WiFi.begin(ssid, password);
  for (int t = 0; WiFi.status() != WL_CONNECTED && t < 30; t++) {
    delay(1000);
    showAll(CRGB::Blue, t + 1);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi : échec");
    blink(CRGB::Red, 5, 10, 100);
    delay(1000);
    ESP.restart();
  }

  showAll(CRGB::Green, ledCount);
  WiFiClient client;
  client.setTimeout(60000);
#if defined(ESP8266)
  POULPY_HTTPUPDATE.setClientTimeout(60000);
#endif
  t_httpUpdate_return result = POULPY_HTTPUPDATE.update(client, firmwareUrl);

  if (result == HTTP_UPDATE_FAILED) {
    Serial.printf("Mise à jour échouée : %s\n", POULPY_HTTPUPDATE.getLastErrorString().c_str());
    blink(CRGB::Red, ledCount, 20, 50);
  } else if (result == HTTP_UPDATE_NO_UPDATES) {
    showAll(CRGB::Orange, ledCount);
    delay(2000);
  } else {
    showAll(CRGB::White, ledCount);
    delay(1000);
  }
  ESP.restart();
}

// ---------- setup / loop ----------

void setup() {
  bootTime = millis();
  Serial.begin(115200);
  Serial.printf("\nPoulpyTube %s\n", VERSION);

  configBegin();
  ledCount = configLoadLedCount();
  setupTubeNumber = configLoadGroup();
  setupAddress = configLoadAddress(setupTubeNumber);
  configLoadSettings(settings);
  autoLookBegin(settings.autoLook);

  FastLED.addLeds<WS2812, POULPY_LED_PIN, GRB>(leds, ledCount);
  powerBudgetLoad();

  executeOTAUpdate();

  WiFi.disconnect();
#if defined(ESP8266)
  ESP.eraseConfig();  // sans ça, ESP-NOW ne reçoit parfois rien
#endif
  WiFi.persistent(false);  // la passerelle gère son Wi-Fi elle-même, rien en flash
  WiFi.mode(WIFI_STA);
  WiFi.macAddress(myMac);
  Serial.printf("MAC %s | groupe %d | adresse %d | %d LEDs\n",
                WiFi.macAddress().c_str(), setupTubeNumber + 1, setupAddress, ledCount);

  detectedChannel = configLoadChannel();
  if (detectedChannel == 0) {
    detectedChannel = scanForChannel();
    if (detectedChannel == 0) detectedChannel = 1;
    configSaveChannel(detectedChannel);
  }
  Serial.printf("Canal %d\n", detectedChannel);

  startEspNow(detectedChannel);

  bool scanMacValid = false;
  for (int i = 0; i < 6; i++)
    if (senderMacFromScan[i] != 0x00 && senderMacFromScan[i] != 0xFF) scanMacValid = true;
  if (scanMacValid)
    esp_now_add_peer(senderMacFromScan, ESP_NOW_ROLE_COMBO, detectedChannel, NULL, 0);

  gatewayBegin();
}

void loop() {
  processCommands();

  unsigned long now = millis();
  if (restartPending && (long)(now - restartAt) >= 0) ESP.restart();

  if (statsStreamActive && now - lastStatsStreamTime >= 1000) {
    lastStatsStreamTime = now;
    sendStatsResponse();
  }

  if (identifyActive && now - identifySince >= IDENTIFY_MS) identifyActive = false;

  if (settingsDirty && now - settingsDirtyAt >= 3000) {
    settingsDirty = false;
    configSaveSettings(settings);
  }

  gatewayLoop(now);

  if (now - lastFrameMs < framePeriodMs) return;
  lastFrameMs = now;

  FastLED.clear();
  if (remoteConfigActive) {
    renderRemoteConfig(now);
    framePeriodMs = FRAME_MS;
  } else if (identifyActive) {
    renderIdentify(now);
    framePeriodMs = FRAME_MS;
  } else {
    framePeriodMs = renderFrame(now);
  }
  poulpyShow();
}
