#include "gateway.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <LittleFS.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
using WebServerT = ESP8266WebServer;
#else
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
using WebServerT = WebServer;
#endif

#include "autolook.h"
#include "build_version.h"
#include "multifixure.h"
#include "power_budget.h"
#include "tube.h"
#include "web_page.h"

namespace {

const uint32_t BEACON_MS = 1500;
const uint32_t FALLBACK_MS = 30000;      // sans balise pendant 30 s, une barre devient passerelle
const uint32_t STATUS_MS = 5000;
const uint32_t GATEWAY_LOST_MS = 10000;  // plus de statut si la passerelle se tait
const uint32_t TUBE_EXPIRE_MS = 20000;
const uint32_t SAVE_DELAY_MS = 3000;
const uint32_t REPEAT_GAP_MS = 30;       // écart entre les répétitions d'un même LOOK
const uint32_t SEND_GAP_MS = 4;          // écart entre deux paquets
const uint8_t LOOK_REPEATS = 3;
const uint16_t REBROADCAST_FADE_MS = 500;
const uint32_t WIFI_RETRY_MS = 120000;
const uint32_t WIFI_CONNECT_MS = 20000;
const uint32_t FULL_BATTERY_MWH = 9500;  // même référence que Pollux

const uint8_t MAX_TUBES = 24;
const uint8_t MAX_INDIV = 16;
const uint8_t SCENE_COUNT = 20;
const uint8_t MAX_CMDS = 8;

const char* const STATE_FILE = "/state.bin";
const char* const SCENES_FILE = "/scenes.bin";

enum Role : uint8_t { ROLE_TUBE, ROLE_FALLBACK, ROLE_DESIGNATED };
Role role = ROLE_TUBE;

// ---------- Côté barre ----------
uint32_t lastBeaconMs = 0;
uint32_t fallbackAfterMs = FALLBACK_MS;
bool gatewayHeard = false;
uint32_t nextStatusMs = 0;

// ---------- État de la passerelle ----------
struct IndivLook {
  uint8_t mac[6];
  uint8_t look[LOOK_SIZE];
};

uint8_t groupLooks[GROUP_COUNT][LOOK_SIZE];
uint16_t knownGroups = 0;  // groupes dont on connaît le look (posé ou appris des barres)
IndivLook indiv[MAX_INDIV];
uint8_t indivCount = 0;
int8_t activeScene = -1;
uint16_t stateVersion = 0;
bool stateDirty = false;
uint32_t stateDirtyAt = 0;

struct TubeRow {
  uint8_t mac[6];
  uint8_t group;
  uint16_t address, leds, watts;
  uint32_t mwh;
  uint8_t src, flags;
  uint8_t look[LOOK_SIZE];
  char fw[17];
  uint32_t seen;
};
TubeRow rows[MAX_TUBES];
uint8_t rowCount = 0;

struct Scene {
  uint8_t used;
  char name[41];
  uint16_t fadeMs;
  uint8_t groups[GROUP_COUNT][LOOK_SIZE];
  uint8_t count;
  IndivLook tubes[MAX_INDIV];
};

// ---------- Envoi ----------
// Un compteur de répétitions par cible : le paquet est construit au moment de l'envoi,
// avec l'état courant. Un look modifié 10 fois par seconde ne remplit aucune file.
struct LookSend {
  uint8_t repeats;
  uint8_t flags;
  uint16_t fadeMs;
  uint32_t lastMs;
};
LookSend groupSend[GROUP_COUNT];
LookSend indivSend[MAX_INDIV];

struct CmdSend {
  uint8_t code;
  uint8_t mac[6];
  uint8_t payload[4];
  uint8_t len;
  uint8_t repeats;
  uint32_t lastMs;
};
CmdSend cmds[MAX_CMDS];
uint8_t cmdCount = 0;

uint32_t lastSendMs = 0;
uint32_t lastBeaconSentMs = 0;
bool statusRequest = false;
uint8_t sendCursor = 0;

// ---------- Wi-Fi et web ----------
WebServerT server(80);
DNSServer dns;
char apSsid[20];
enum WifiStep : uint8_t { W_OFF, W_WAIT, W_SCAN, W_CONNECT, W_UP };
WifiStep wifiStep = W_OFF;
uint32_t wifiAt = 0;
int routerChannel = 0;

const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool sameMac(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

String macText(const uint8_t* m) {
  char s[18];
  snprintf(s, sizeof(s), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(s);
}

bool parseMac(const char* s, uint8_t* out) {
  if (!s) return false;
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

void header(struct_dmx_packet& p, uint8_t code, const uint8_t* mac) {
  memset(&p, 0, sizeof(p));
  p.blockNumber = CMD_BLOCK;
  p.data[0] = SIGNATURE_A;
  p.data[1] = SIGNATURE_B;
  p.data[2] = SIGNATURE_C;
  p.data[3] = code;
  memcpy(&p.data[CMD_TARGET_MAC], mac ? mac : BROADCAST, 6);
}

void put16(uint8_t* d, uint16_t v) {
  d[0] = v & 0xFF;
  d[1] = v >> 8;
}

uint16_t get16(const uint8_t* d) { return d[0] | (d[1] << 8); }

uint8_t effectiveGroupOf(const uint8_t* mac) {
  for (uint8_t i = 0; i < rowCount; i++)
    if (sameMac(rows[i].mac, mac)) return rows[i].group;
  return 0xFF;
}

int findIndiv(const uint8_t* mac) {
  for (uint8_t i = 0; i < indivCount; i++)
    if (sameMac(indiv[i].mac, mac)) return i;
  return -1;
}

void removeIndiv(int i) {
  for (uint8_t k = i; k + 1 < indivCount; k++) {
    indiv[k] = indiv[k + 1];
    indivSend[k] = indivSend[k + 1];
  }
  indivCount--;
}

void markState() {
  stateVersion++;
  stateDirty = true;
  stateDirtyAt = millis();
}

// ---------- Paquets ----------

void buildGroupLook(struct_dmx_packet& p, uint8_t g, uint8_t flags, uint16_t fadeMs) {
  header(p, LOOK_CMD, nullptr);
  put16(&p.data[LOOK_MASK], (uint16_t)(1u << g));
  memcpy(&p.data[LOOK_DATA], groupLooks[g], LOOK_SIZE);
  put16(&p.data[LOOK_FADE], fadeMs);
  put16(&p.data[LOOK_VERSION], stateVersion);
  p.data[LOOK_FLAGS] = flags;
}

void buildIndivLook(struct_dmx_packet& p, uint8_t i, uint16_t fadeMs) {
  header(p, LOOK_CMD, indiv[i].mac);
  memcpy(&p.data[LOOK_DATA], indiv[i].look, LOOK_SIZE);
  put16(&p.data[LOOK_FADE], fadeMs);
  put16(&p.data[LOOK_VERSION], stateVersion);
}

// La passerelle est aussi une barre : elle s'applique le look tout de suite.
void sendGroupLook(uint8_t g, uint8_t flags, uint16_t fadeMs) {
  struct_dmx_packet p;
  buildGroupLook(p, g, flags, fadeMs);
  tubeProcessPacket(p);
  groupSend[g] = {LOOK_REPEATS, flags, fadeMs, 0};
}

void sendIndivLook(uint8_t i, uint16_t fadeMs) {
  struct_dmx_packet p;
  buildIndivLook(p, i, fadeMs);
  tubeProcessPacket(p);
  indivSend[i] = {LOOK_REPEATS, 0, fadeMs, 0};
}

void queueCmd(uint8_t code, const uint8_t* mac, const uint8_t* payload, uint8_t len, uint8_t repeats = 2) {
  struct_dmx_packet p;
  header(p, code, mac);
  memcpy(&p.data[CMD_PAYLOAD], payload, len);
  if (mac && sameMac(mac, myMac)) {
    tubeProcessPacket(p);
    return;
  }
  if (!mac) tubeProcessPacket(p);  // diffusé : la passerelle est concernée aussi
  if (cmdCount >= MAX_CMDS) return;
  CmdSend& c = cmds[cmdCount++];
  c.code = code;
  memcpy(c.mac, mac ? mac : BROADCAST, 6);
  memcpy(c.payload, payload, len);
  c.len = len;
  c.repeats = repeats;
  c.lastMs = 0;
}

void sendBeacon(uint32_t now) {
  struct_dmx_packet p;
  header(p, GATEWAY_BEACON_CMD, myMac);
  for (int i = 0; i < 4; i++) p.data[10 + i] = (now >> (8 * i)) & 0xFF;
  put16(&p.data[14], stateVersion);
  p.data[16] = detectedChannel;
  p.data[17] = (role == ROLE_DESIGNATED ? BEACON_DESIGNATED : 0) | (statusRequest ? BEACON_STATUS_REQ : 0);
  p.data[18] = settings.returnDelayS;
  tubeBroadcast(p);
  statusRequest = false;
  lastBeaconSentMs = now;
}

// Rediffusion : chaque look connu repart une fois, avec un fondu doux si une barre l'avait raté.
void rebroadcast() {
  for (uint8_t g = 0; g < GROUP_COUNT; g++)
    if ((knownGroups & (1u << g)) && groupSend[g].repeats == 0)
      groupSend[g] = {1, 0, REBROADCAST_FADE_MS, 0};
  for (uint8_t i = 0; i < indivCount; i++)
    if (indivSend[i].repeats == 0) indivSend[i] = {1, 0, REBROADCAST_FADE_MS, 0};
}

// Un paquet au plus tous les SEND_GAP_MS ; chaque cible attend REPEAT_GAP_MS entre deux répétitions.
void pumpSends(uint32_t now) {
  if (now - lastSendMs < SEND_GAP_MS) return;
  // Un look de groupe « reset » efface les looks individuels : ceux-ci partent après.
  bool resetPending = false;
  for (uint8_t g = 0; g < GROUP_COUNT; g++)
    resetPending |= groupSend[g].repeats && (groupSend[g].flags & LOOK_FLAG_RESET);
  const uint8_t total = GROUP_COUNT + MAX_INDIV + MAX_CMDS;
  for (uint8_t n = 0; n < total; n++) {
    uint8_t k = (sendCursor + n) % total;
    struct_dmx_packet p;
    if (k < GROUP_COUNT) {
      LookSend& s = groupSend[k];
      if (!s.repeats || (s.lastMs && now - s.lastMs < REPEAT_GAP_MS)) continue;
      buildGroupLook(p, k, s.flags, s.fadeMs);
      s.repeats--;
      s.lastMs = now;
    } else if (k < GROUP_COUNT + MAX_INDIV) {
      uint8_t i = k - GROUP_COUNT;
      if (i >= indivCount || resetPending) continue;
      LookSend& s = indivSend[i];
      if (!s.repeats || (s.lastMs && now - s.lastMs < REPEAT_GAP_MS)) continue;
      buildIndivLook(p, i, s.fadeMs);
      s.repeats--;
      s.lastMs = now;
    } else {
      uint8_t i = k - GROUP_COUNT - MAX_INDIV;
      if (i >= cmdCount) continue;
      CmdSend& c = cmds[i];
      if (c.lastMs && now - c.lastMs < REPEAT_GAP_MS) continue;
      header(p, c.code, c.mac);
      memcpy(&p.data[CMD_PAYLOAD], c.payload, c.len);
      c.lastMs = now;
      if (--c.repeats == 0) {
        for (uint8_t j = i; j + 1 < cmdCount; j++) cmds[j] = cmds[j + 1];
        cmdCount--;
      }
    }
    tubeBroadcast(p);
    lastSendMs = now;
    sendCursor = (k + 1) % total;
    return;
  }
}

// ---------- Statut des barres ----------

void fillOwnStatus(TubeRow& r) {
  memcpy(r.mac, myMac, 6);
  r.group = setupTubeNumber;
  r.address = setupAddress;
  r.leds = ledCount;
  r.watts = powerBudgetLimitW();
  r.mwh = powerBudgetEnergyMwh();
  r.src = tubeIsLive() ? 1 : 0;
  r.flags = (settings.individual ? STATUS_INDIVIDUAL : 0) | (role != ROLE_TUBE ? STATUS_GATEWAY : 0);
  memcpy(r.look, autoLookTarget(), LOOK_SIZE);
  strncpy(r.fw, VERSION, sizeof(r.fw) - 1);
  r.fw[sizeof(r.fw) - 1] = '\0';
}

void sendStatus() {
  TubeRow r;
  fillOwnStatus(r);
  struct_dmx_packet p;
  header(p, TUBE_STATUS_CMD, myMac);
  uint8_t* d = p.data;
  d[10] = r.group;
  put16(&d[11], r.address);
  put16(&d[13], r.leds);
  put16(&d[15], r.watts);
  for (int i = 0; i < 4; i++) d[17 + i] = (r.mwh >> (8 * i)) & 0xFF;
  d[21] = r.src;
  d[22] = r.flags;
  memcpy(&d[23], r.look, LOOK_SIZE);
  uint8_t len = strlen(r.fw);
  d[33] = len;
  memcpy(&d[34], r.fw, len);
  tubeBroadcast(p);
}

TubeRow* rowFor(const uint8_t* mac, uint32_t now) {
  for (uint8_t i = 0; i < rowCount; i++)
    if (sameMac(rows[i].mac, mac)) return &rows[i];
  // place libre, sinon la plus ancienne
  uint8_t slot = rowCount;
  if (rowCount >= MAX_TUBES) {
    slot = 0;
    for (uint8_t i = 1; i < rowCount; i++)
      if (rows[i].seen < rows[slot].seen) slot = i;
  } else {
    rowCount++;
  }
  memset(&rows[slot], 0, sizeof(TubeRow));
  memcpy(rows[slot].mac, mac, 6);
  rows[slot].seen = now;
  return &rows[slot];
}

// Une passerelle neuve (sans état enregistré) apprend les looks des barres au lieu de les écraser.
void learnFrom(const TubeRow& r) {
  if (r.flags & STATUS_INDIVIDUAL) {
    if (findIndiv(r.mac) < 0 && indivCount < MAX_INDIV) {
      memcpy(indiv[indivCount].mac, r.mac, 6);
      memcpy(indiv[indivCount].look, r.look, LOOK_SIZE);
      indivSend[indivCount] = {0, 0, 0, 0};
      indivCount++;
    }
    return;
  }
  if (r.group < GROUP_COUNT && !(knownGroups & (1u << r.group))) {
    memcpy(groupLooks[r.group], r.look, LOOK_SIZE);
    knownGroups |= 1u << r.group;
  }
}

void expireRows(uint32_t now) {
  for (uint8_t i = 0; i < rowCount;) {
    if (!sameMac(rows[i].mac, myMac) && now - rows[i].seen > TUBE_EXPIRE_MS) {
      rows[i] = rows[--rowCount];
    } else {
      i++;
    }
  }
}

// ---------- Fichiers ----------

void saveState() {
  File f = LittleFS.open(STATE_FILE, "w");
  if (!f) return;
  f.write((uint8_t)'S');
  f.write((uint8_t)1);
  f.write((const uint8_t*)groupLooks, sizeof(groupLooks));
  f.write((const uint8_t*)&knownGroups, sizeof(knownGroups));
  f.write((uint8_t)activeScene);
  f.write(indivCount);
  f.write((const uint8_t*)indiv, sizeof(IndivLook) * indivCount);
  f.close();
  stateDirty = false;
}

void loadState() {
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  if (f.read() == 'S' && f.read() == 1) {
    f.read((uint8_t*)groupLooks, sizeof(groupLooks));
    f.read((uint8_t*)&knownGroups, sizeof(knownGroups));
    activeScene = (int8_t)f.read();
    if (activeScene >= SCENE_COUNT) activeScene = -1;
    indivCount = min((int)f.read(), (int)MAX_INDIV);
    f.read((uint8_t*)indiv, sizeof(IndivLook) * indivCount);
  }
  f.close();
}

bool readScene(uint8_t slot, Scene& s) {
  memset(&s, 0, sizeof(s));
  File f = LittleFS.open(SCENES_FILE, "r");
  if (!f) return false;
  bool ok = f.seek((uint32_t)slot * sizeof(Scene)) && f.read((uint8_t*)&s, sizeof(s)) == sizeof(s);
  f.close();
  if (!ok || s.used != 1) {
    memset(&s, 0, sizeof(s));
    return false;
  }
  s.name[sizeof(s.name) - 1] = '\0';
  if (s.count > MAX_INDIV) s.count = MAX_INDIV;
  return true;
}

void writeScene(uint8_t slot, const Scene& s) {
  if (!LittleFS.exists(SCENES_FILE)) {
    File f = LittleFS.open(SCENES_FILE, "w");
    Scene empty;
    memset(&empty, 0, sizeof(empty));
    for (uint8_t i = 0; i < SCENE_COUNT; i++) f.write((const uint8_t*)&empty, sizeof(empty));
    f.close();
  }
  File f = LittleFS.open(SCENES_FILE, "r+");
  if (!f) return;
  f.seek((uint32_t)slot * sizeof(Scene));
  f.write((const uint8_t*)&s, sizeof(s));
  f.close();
}

void recallScene(uint8_t slot) {
  Scene s;
  if (!readScene(slot, s)) return;
  memcpy(groupLooks, s.groups, sizeof(groupLooks));
  knownGroups = (1u << GROUP_COUNT) - 1;
  indivCount = s.count;
  memcpy(indiv, s.tubes, sizeof(IndivLook) * indivCount);
  for (uint8_t g = 0; g < GROUP_COUNT; g++) sendGroupLook(g, LOOK_FLAG_RESET, s.fadeMs);
  for (uint8_t i = 0; i < indivCount; i++) sendIndivLook(i, s.fadeMs);
  activeScene = slot;
  markState();
}

// ---------- JSON ----------

void lookToJson(JsonArray a, const uint8_t* look) {
  for (int i = 0; i < LOOK_SIZE; i++) a.add(look[i]);
}

bool lookFromJson(JsonVariantConst v, uint8_t* look) {
  JsonArrayConst a = v.as<JsonArrayConst>();
  if (a.isNull() || a.size() != LOOK_SIZE) return false;
  for (int i = 0; i < LOOK_SIZE; i++) look[i] = a[i].as<uint8_t>();
  return true;
}

void sendJson(JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

void sendError(const char* msg) {
  String s = String("{\"ok\":false,\"error\":\"") + msg + "\"}";
  server.send(400, "application/json", s);
}

bool readBody(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain"))) {
    sendError("json");
    return false;
  }
  return true;
}

void sceneToJson(JsonObject o, uint8_t slot, const Scene& s) {
  o["slot"] = slot;
  o["name"] = s.name;
  o["fadeMs"] = s.fadeMs;
  JsonArray groups = o["groups"].to<JsonArray>();
  for (uint8_t g = 0; g < GROUP_COUNT; g++) lookToJson(groups.add<JsonArray>(), s.groups[g]);
  JsonArray tubes = o["tubes"].to<JsonArray>();
  for (uint8_t i = 0; i < s.count; i++) {
    JsonObject t = tubes.add<JsonObject>();
    t["mac"] = macText(s.tubes[i].mac);
    lookToJson(t["look"].to<JsonArray>(), s.tubes[i].look);
  }
}

// ---------- API ----------

void handlePage() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", (const char*)WEB_PAGE_GZ, WEB_PAGE_GZ_LEN);
}

void handleNotFound() {
  if (server.uri().startsWith("/api/")) {
    server.send(404, "application/json", "{\"ok\":false}");
    return;
  }
  handlePage();  // portail captif : toute adresse mène à la page
}

void handleState() {
  JsonDocument doc;
  bool live = false;
  for (uint8_t i = 0; i < rowCount; i++) live |= rows[i].src == 1;
  doc["mode"] = live ? "live" : "auto";
  doc["fw"] = VERSION;
  JsonObject gw = doc["gateway"].to<JsonObject>();
  gw["mac"] = macText(myMac);
  gw["designated"] = role == ROLE_DESIGNATED;
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["ssid"] = settings.wifiSsid;
  w["ap"] = wifiStep != W_UP;
  w["ip"] = wifiStep == W_UP ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  w["apSsid"] = apSsid;
  w["channel"] = detectedChannel;
  w["routerChannel"] = routerChannel;
  doc["returnDelay"] = settings.returnDelayS;
  doc["activeScene"] = activeScene;
  JsonArray groups = doc["groups"].to<JsonArray>();
  for (uint8_t g = 0; g < GROUP_COUNT; g++) {
    JsonObject o = groups.add<JsonObject>();
    o["n"] = g + 1;
    lookToJson(o["look"].to<JsonArray>(), groupLooks[g]);
    uint8_t count = 0;
    for (uint8_t i = 0; i < rowCount; i++) count += rows[i].group == g;
    o["count"] = count;
  }
  sendJson(doc);
}

void handleTubes() {
  JsonDocument doc;
  JsonArray a = doc.to<JsonArray>();
  for (uint8_t i = 0; i < rowCount; i++) {
    const TubeRow& r = rows[i];
    JsonObject o = a.add<JsonObject>();
    o["mac"] = macText(r.mac);
    o["group"] = r.group + 1;
    o["address"] = r.address;
    o["leds"] = r.leds;
    o["watts"] = r.watts;
    o["mwh"] = r.mwh;
    o["batt"] = r.mwh >= FULL_BATTERY_MWH ? 0 : (int)(100 - r.mwh * 100 / FULL_BATTERY_MWH);
    o["fw"] = r.fw;
    o["src"] = r.src ? "live" : "auto";
    o["gw"] = (r.flags & STATUS_GATEWAY) != 0;
    int k = findIndiv(r.mac);
    if (k >= 0) lookToJson(o["look"].to<JsonArray>(), indiv[k].look);
  }
  sendJson(doc);
}

void handleLook() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  uint8_t look[LOOK_SIZE];
  if (!lookFromJson(doc["look"], look)) return sendError("look");
  uint16_t fadeMs = doc["fadeMs"] | 300;

  uint8_t mac[6];
  if (parseMac(doc["mac"].as<const char*>(), mac)) {
    int i = findIndiv(mac);
    if (i < 0) {
      if (indivCount >= MAX_INDIV) return sendError("full");
      i = indivCount++;
      memcpy(indiv[i].mac, mac, 6);
    }
    memcpy(indiv[i].look, look, LOOK_SIZE);
    sendIndivLook(i, fadeMs);
  } else {
    uint16_t mask = 0;
    JsonVariantConst groups = doc["groups"];
    if (groups.is<const char*>()) {
      mask = (1u << GROUP_COUNT) - 1;  // "all"
    } else {
      for (JsonVariantConst v : groups.as<JsonArrayConst>()) {
        int n = v.as<int>();
        if (n >= 1 && n <= GROUP_COUNT) mask |= 1u << (n - 1);
      }
    }
    if (!mask) return sendError("groups");
    for (uint8_t g = 0; g < GROUP_COUNT; g++) {
      if (!(mask & (1u << g))) continue;
      memcpy(groupLooks[g], look, LOOK_SIZE);
      knownGroups |= 1u << g;
      for (int i = indivCount - 1; i >= 0; i--)
        if (effectiveGroupOf(indiv[i].mac) == g) removeIndiv(i);
      sendGroupLook(g, LOOK_FLAG_RESET, fadeMs);
    }
  }
  activeScene = -1;
  markState();
  sendOk();
}

void handleScan() {
  statusRequest = true;
  lastBeaconSentMs = 0;  // balise tout de suite
  sendOk();
}

void handleTube() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  uint8_t mac[6];
  if (!parseMac(doc["mac"].as<const char*>(), mac)) return sendError("mac");
  uint8_t pl[4];
  TubeRow* row = nullptr;
  for (uint8_t i = 0; i < rowCount; i++)
    if (sameMac(rows[i].mac, mac)) row = &rows[i];

  if (doc["identify"] | false) queueCmd(IDENTIFY_CMD, mac, pl, 0);
  if (doc["group"].is<int>()) {
    int g = doc["group"].as<int>();
    if (g < 1 || g > GROUP_COUNT) return sendError("group");
    pl[0] = g - 1;
    queueCmd(SET_GROUP_NUMBER_CMD, mac, pl, 1);
    if (row) row->group = g - 1;
  }
  if (doc["address"].is<int>()) {
    int a = doc["address"].as<int>();
    if (a < 0 || a > 512 - MULTIFIXTURE_SLOT + 1) return sendError("address");
    put16(pl, a);
    queueCmd(SET_DMX_ADDRESS_CMD, mac, pl, 2);
    if (row) row->address = a;
  }
  if (doc["leds"].is<int>()) {
    int n = doc["leds"].as<int>();
    if (n < 1 || n > MAXLEDLENGTH) return sendError("leds");
    put16(pl, n);
    queueCmd(SET_LED_COUNT_CMD, mac, pl, 2, 1);  // la barre redémarre : une fois suffit
    if (row) row->leds = n;
  }
  if (doc["wattLimit"].is<int>()) {
    int w = doc["wattLimit"].as<int>();
    if (w < 0 || w > 200) return sendError("watts");
    put16(pl, w);
    queueCmd(SET_POWER_LIMIT_CMD, mac, pl, 2);
    if (row) row->watts = w;
  }
  if (doc["role"] == "gateway" && !sameMac(mac, myMac)) {
    pl[0] = 1;
    queueCmd(SET_ROLE_CMD, mac, pl, 1, 3);
    settings.role = 0;
    configSaveSettings(settings);
    saveState();
    tubeRestart(1500);  // laisser partir SET_ROLE
  }
  sendOk();
}

void handleSettings() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  if (doc["wifiSsid"].is<const char*>()) {
    strncpy(settings.wifiSsid, doc["wifiSsid"].as<const char*>(), sizeof(settings.wifiSsid) - 1);
    strncpy(settings.wifiPass, doc["wifiPass"] | "", sizeof(settings.wifiPass) - 1);
    configSaveSettings(settings);
    WiFi.disconnect(false);
    routerChannel = 0;
    wifiStep = settings.wifiSsid[0] ? W_WAIT : W_OFF;
    wifiAt = millis();
  }
  if (doc["returnDelay"].is<int>()) {
    int d = doc["returnDelay"].as<int>();
    if (d >= 1 && d <= 30) {
      settings.returnDelayS = d;
      configSaveSettings(settings);
      lastBeaconSentMs = 0;
    }
  }
  if (doc["channel"].is<int>()) {
    int ch = doc["channel"].as<int>();
    if (ch < 1 || ch > 13) return sendError("channel");
    uint8_t pl[1] = {(uint8_t)ch};
    saveState();
    queueCmd(SET_CHANNEL_CMD, nullptr, pl, 1, 3);  // toutes les barres, la passerelle comprise
  }
  sendOk();
}

void handleScenesGet() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  bool first = true;
  for (uint8_t slot = 0; slot < SCENE_COUNT; slot++) {
    Scene s;
    if (!readScene(slot, s)) continue;
    JsonDocument doc;
    sceneToJson(doc.to<JsonObject>(), slot, s);
    String out = first ? "" : ",";
    serializeJson(doc, out);
    server.sendContent(out);
    first = false;
  }
  server.sendContent("]");
  server.sendContent("");
}

// Sans "groups" : enregistre l'état actuel. Avec : écrit la scène telle quelle (restauration).
void handleScenePost() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= SCENE_COUNT) return sendError("slot");
  Scene s;
  memset(&s, 0, sizeof(s));
  s.used = 1;
  strncpy(s.name, doc["name"] | "", sizeof(s.name) - 1);
  s.fadeMs = doc["fadeMs"] | 1000;
  JsonArrayConst groups = doc["groups"].as<JsonArrayConst>();
  if (groups.isNull()) {
    memcpy(s.groups, groupLooks, sizeof(groupLooks));
    s.count = indivCount;
    memcpy(s.tubes, indiv, sizeof(IndivLook) * indivCount);
    activeScene = slot;
    markState();
  } else {
    for (uint8_t g = 0; g < GROUP_COUNT && g < groups.size(); g++) lookFromJson(groups[g], s.groups[g]);
    for (JsonVariantConst t : doc["tubes"].as<JsonArrayConst>()) {
      if (s.count >= MAX_INDIV) break;
      if (parseMac(t["mac"].as<const char*>(), s.tubes[s.count].mac) && lookFromJson(t["look"], s.tubes[s.count].look)) s.count++;
    }
  }
  writeScene(slot, s);
  sendOk();
}

void handleSceneMeta() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  int slot = doc["slot"] | -1;
  Scene s;
  if (slot < 0 || slot >= SCENE_COUNT || !readScene(slot, s)) return sendError("slot");
  if (doc["name"].is<const char*>()) strncpy(s.name, doc["name"].as<const char*>(), sizeof(s.name) - 1);
  if (doc["fadeMs"].is<int>()) s.fadeMs = doc["fadeMs"].as<int>();
  writeScene(slot, s);
  sendOk();
}

void handleSceneRecall() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= SCENE_COUNT) return sendError("slot");
  recallScene(slot);
  sendOk();
}

void handleSceneDelete() {
  JsonDocument doc;
  if (!readBody(doc)) return;
  int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= SCENE_COUNT) return sendError("slot");
  Scene s;
  memset(&s, 0, sizeof(s));
  writeScene(slot, s);
  if (activeScene == slot) {
    activeScene = -1;
    markState();
  }
  sendOk();
}

// ---------- Démarrage / Wi-Fi ----------

void startMdns() {
#if defined(ESP8266)
  MDNS.close();
#else
  MDNS.end();
#endif
  if (MDNS.begin("poulpytube")) MDNS.addService("http", "tcp", 80);
}

void startGateway(Role r) {
  role = r;
  Serial.printf("Passerelle (%s)\n", r == ROLE_DESIGNATED ? "désignée" : "secours");
#if defined(ESP8266)
  LittleFS.begin();
#else
  LittleFS.begin(true);
#endif
  loadState();

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_AP_STA);
  snprintf(apSsid, sizeof(apSsid), "PoulpyTube-%02X%02X", myMac[4], myMac[5]);
  WiFi.softAP(apSsid, nullptr, detectedChannel);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handlePage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/tubes", HTTP_GET, handleTubes);
  server.on("/api/look", HTTP_POST, handleLook);
  server.on("/api/scan", HTTP_POST, handleScan);
  server.on("/api/tube", HTTP_POST, handleTube);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/scenes", HTTP_GET, handleScenesGet);
  server.on("/api/scenes", HTTP_POST, handleScenePost);
  server.on("/api/scenes/meta", HTTP_POST, handleSceneMeta);
  server.on("/api/scenes/recall", HTTP_POST, handleSceneRecall);
  server.on("/api/scenes/delete", HTTP_POST, handleSceneDelete);
  server.onNotFound(handleNotFound);
  server.begin();
  startMdns();

  wifiStep = settings.wifiSsid[0] ? W_WAIT : W_OFF;
  wifiAt = millis();
  statusRequest = true;
  lastBeaconSentMs = 0;
  Serial.printf("Wi-Fi %s ouvert, http://%s\n", apSsid, WiFi.softAPIP().toString().c_str());
}

// Le Wi-Fi de la maison n'est rejoint que s'il est sur le canal des barres :
// en changer déplacerait les barres hors du canal de PoulpyNode / PoulpyLink.
void wifiLoop(uint32_t now) {
  switch (wifiStep) {
    case W_OFF:
      break;
    case W_WAIT:
      if ((int32_t)(now - wifiAt) >= 0) {
        WiFi.scanNetworks(true);
        wifiStep = W_SCAN;
      }
      break;
    case W_SCAN: {
      int n = WiFi.scanComplete();
      if (n == WIFI_SCAN_RUNNING) break;
      int found = -1;
      for (int i = 0; i < n; i++)
        if (WiFi.SSID(i) == settings.wifiSsid) found = i;
      routerChannel = found >= 0 ? WiFi.channel(found) : 0;
      if (found >= 0 && routerChannel == detectedChannel) {
        uint8_t bssid[6];
        memcpy(bssid, WiFi.BSSID(found), 6);
        WiFi.scanDelete();
        WiFi.begin(settings.wifiSsid, settings.wifiPass, routerChannel, bssid);
        wifiStep = W_CONNECT;
        wifiAt = now + WIFI_CONNECT_MS;
      } else {
        WiFi.scanDelete();
        Serial.printf("Wi-Fi %s : %s\n", settings.wifiSsid,
                      found < 0 ? "introuvable" : "autre canal que les barres");
        wifiStep = W_WAIT;
        wifiAt = now + WIFI_RETRY_MS;
      }
      break;
    }
    case W_CONNECT:
      if (WiFi.status() == WL_CONNECTED) {
        wifiStep = W_UP;
        Serial.printf("Wi-Fi %s : %s\n", settings.wifiSsid, WiFi.localIP().toString().c_str());
        startMdns();
      } else if ((int32_t)(now - wifiAt) >= 0) {
        WiFi.disconnect(false);
        wifiStep = W_WAIT;
        wifiAt = now + WIFI_RETRY_MS;
      }
      break;
    case W_UP:
      if (WiFi.status() != WL_CONNECTED) {
        wifiStep = W_WAIT;
        wifiAt = now + 5000;
      }
      break;
  }
}

}  // namespace

// ---------- Interface ----------

bool gatewayActive() { return role != ROLE_TUBE; }

void gatewayBegin() {
  lastBeaconMs = millis();
  fallbackAfterMs = FALLBACK_MS + (myMac[5] % 16) * 500;  // évite que toutes partent ensemble
  nextStatusMs = millis() + 1000 + (myMac[5] % 32) * 50;
  if (settings.role == 1) startGateway(ROLE_DESIGNATED);
}

void gatewayOnLook(const uint8_t* d) {
  uint16_t fadeMs = get16(&d[LOOK_FADE]);
  bool byMac = !sameMac(&d[CMD_TARGET_MAC], BROADCAST);
  if (byMac) {
    if (!sameMac(&d[CMD_TARGET_MAC], myMac)) return;
    if (!settings.individual) {
      settings.individual = 1;
      tubeSaveSettingsSoon();
    }
  } else {
    uint16_t mask = get16(&d[LOOK_MASK]);
    if (!(mask & (1u << setupTubeNumber))) return;
    if (settings.individual) {
      if (!(d[LOOK_FLAGS] & LOOK_FLAG_RESET)) return;
      settings.individual = 0;
      tubeSaveSettingsSoon();
    }
  }
  if (autoLookSet(&d[LOOK_DATA], fadeMs)) {
    memcpy(settings.autoLook, &d[LOOK_DATA], LOOK_SIZE);
    tubeSaveSettingsSoon();
  }
}

void gatewayOnBeacon(const uint8_t* d) {
  const uint8_t* mac = &d[CMD_TARGET_MAC];
  if (sameMac(mac, myMac)) return;
  bool otherDesignated = d[17] & BEACON_DESIGNATED;
  if (role != ROLE_TUBE) {
    bool meDesignated = role == ROLE_DESIGNATED;
    bool yield = (otherDesignated && !meDesignated)
              || (otherDesignated == meDesignated && memcmp(mac, myMac, 6) < 0);
    if (!yield) return;  // l'autre se retirera
    Serial.printf("Passerelle %s prioritaire : retrait\n", macText(mac).c_str());
    if (meDesignated) {
      settings.role = 0;
      configSaveSettings(settings);
    }
    if (stateDirty) saveState();
    tubeRestart(300);
    return;
  }
  uint32_t now = millis();
  lastBeaconMs = now;
  gatewayHeard = true;
  uint8_t delayS = d[18];
  if (delayS >= 1 && delayS <= 30 && delayS != settings.returnDelayS) {
    settings.returnDelayS = delayS;
    tubeSaveSettingsSoon();
  }
  if (d[17] & BEACON_STATUS_REQ) nextStatusMs = now + (myMac[5] % 20) * 15;
}

void gatewayOnSetRole(const uint8_t* d) {
  if (!sameMac(&d[CMD_TARGET_MAC], myMac)) return;
  uint8_t r = d[CMD_PAYLOAD] ? 1 : 0;
  if (settings.role == r && (r == 1) == (role == ROLE_DESIGNATED)) return;
  settings.role = r;
  configSaveSettings(settings);
  if (r == 1 && role == ROLE_FALLBACK) {
    role = ROLE_DESIGNATED;
  } else {
    tubeRestart(300);  // démarrer proprement dans le nouveau rôle
  }
}

void gatewayOnStatus(const uint8_t* d) {
  if (role == ROLE_TUBE) return;
  uint32_t now = millis();
  TubeRow* r = rowFor(&d[CMD_TARGET_MAC], now);
  r->group = d[10] < GROUP_COUNT ? d[10] : 0;
  r->address = get16(&d[11]);
  r->leds = get16(&d[13]);
  r->watts = get16(&d[15]);
  r->mwh = d[17] | (d[18] << 8) | ((uint32_t)d[19] << 16) | ((uint32_t)d[20] << 24);
  r->src = d[21];
  r->flags = d[22];
  memcpy(r->look, &d[23], LOOK_SIZE);
  uint8_t len = min((int)d[33], 16);
  memcpy(r->fw, &d[34], len);
  r->fw[len] = '\0';
  r->seen = now;
  learnFrom(*r);
}

void gatewayLoop(unsigned long now) {
  if (role == ROLE_TUBE) {
    if (now - lastBeaconMs > fallbackAfterMs) {
      startGateway(ROLE_FALLBACK);
      return;
    }
    if (gatewayHeard && now - lastBeaconMs < GATEWAY_LOST_MS && (int32_t)(now - nextStatusMs) >= 0) {
      sendStatus();
      nextStatusMs = now + STATUS_MS + (myMac[5] % 16) * 20;
    }
    return;
  }

  dns.processNextRequest();
  server.handleClient();
#if defined(ESP8266)
  MDNS.update();
#endif
  wifiLoop(now);

  if (now - lastBeaconSentMs >= BEACON_MS) {
    sendBeacon(now);
    rebroadcast();
    TubeRow* own = rowFor(myMac, now);
    fillOwnStatus(*own);
    own->seen = now;
    learnFrom(*own);
    expireRows(now);
  }
  pumpSends(now);

  if (stateDirty && now - stateDirtyAt > SAVE_DELAY_MS) saveState();
}
