#include "artnet.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include <WiFiUdp.h>

#include "build_version.h"
#include "protocol.h"
#include "tube.h"

namespace {

const uint16_t ARTNET_PORT = 6454;
const uint16_t OP_POLL = 0x2000;
const uint16_t OP_POLL_REPLY = 0x2100;
const uint16_t OP_DMX = 0x5000;
const uint32_t KEEPALIVE_MS = 250;     // un bloc inchangé repart quand même, pour une barre qui l'a raté
const uint32_t MIN_BLOCK_GAP_MS = 20;  // 50 trames/s au plus par bloc
const uint32_t FEED_MS = 500;          // au-delà, plus de relais : les barres comptent leur délai de retour
const uint32_t RECEIVING_MS = 2000;
const uint32_t FOREIGN_MS = 1500;
const uint8_t MAX_PACKETS_PER_LOOP = 4;

const uint8_t ARTNET_ID[8] = {'A', 'r', 't', '-', 'N', 'e', 't', 0};

WiFiUDP udp;
bool listening = false;
uint8_t rx[18 + 512];
uint8_t universeData[512];
uint8_t sent[4][128];
bool sentValid[4];
uint32_t sentAt[4];
uint32_t lastDmxMs = 0;
IPAddress lastFrom;
uint16_t framesInSecond = 0;
uint32_t secondStart = 0;
uint8_t fps = 0;
uint16_t pollReplies = 0;

bool foreignLive(uint32_t now) {
  const unsigned long r = tubeRadioLiveMs();
  return r && (long)(now - r) < (long)FOREIGN_MS;
}

void putIp(uint8_t* d, IPAddress ip) {
  for (int i = 0; i < 4; i++) d[i] = ip[i];
}

void sendPollReply(const ArtnetConfig& c, uint32_t now) {
  uint8_t r[239];
  memset(r, 0, sizeof(r));
  memcpy(r, ARTNET_ID, 8);
  r[8] = OP_POLL_REPLY & 0xFF;
  r[9] = OP_POLL_REPLY >> 8;
  putIp(&r[10], c.ip);
  r[14] = ARTNET_PORT & 0xFF;
  r[15] = ARTNET_PORT >> 8;
  r[17] = 1;                           // version du firmware (pas suivie)
  r[18] = (c.universe >> 8) & 0x7F;    // Net
  r[19] = (c.universe >> 4) & 0x0F;    // Sub-Net
  r[23] = 0xD0;                        // voyants normaux, adresse réglée sur le nœud
  strncpy((char*)&r[26], "PoulpyLED", 17);
  snprintf((char*)&r[44], 64, "PoulpyLED passerelle %s", VERSION);
  const ArtnetStatus s = artnetStatus(now);
  snprintf((char*)&r[108], 64, "#0001 [%04u] %s", (unsigned)(++pollReplies % 10000),
           s.suspended ? "relais suspendu : autre emetteur" : (s.receiving ? "relais actif" : "en attente"));
  r[173] = 1;                          // un port
  r[174] = 0x80;                       // sortie DMX512
  r[182] = s.receiving && !s.suspended ? 0x80 : 0x00;
  r[190] = c.universe & 0x0F;
  WiFi.macAddress(&r[201]);
  putIp(&r[207], c.ip);
  r[211] = 1;
  r[212] = 0x08;                       // adresses de port sur 15 bits
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write(r, sizeof(r));
  udp.endPacket();
}

void handlePacket(const ArtnetConfig& c, int len, uint32_t now) {
  if (len < 10 || memcmp(rx, ARTNET_ID, 8) != 0) return;
  const uint16_t op = rx[8] | (rx[9] << 8);
  if (op == OP_POLL) {
    sendPollReply(c, now);
    return;
  }
  if (op != OP_DMX || len < 18) return;
  const uint16_t uni = rx[14] | ((rx[15] & 0x7F) << 8);
  if (uni != c.universe) return;
  int n = (rx[16] << 8) | rx[17];
  if (n > len - 18) n = len - 18;
  if (n > 512) n = 512;
  if (n <= 0) return;
  memcpy(universeData, &rx[18], n);
  lastDmxMs = now;
  lastFrom = udp.remoteIP();
  framesInSecond++;
  if (!foreignLive(now)) tubeFeedLive(universeData);
}

void relay(uint32_t now, uint8_t blocks) {
  for (uint8_t b = 0; b < 4; b++) {
    if (!(blocks & (1 << b))) continue;
    const uint8_t* src = &universeData[b * 128];
    const bool changed = !sentValid[b] || memcmp(sent[b], src, 128) != 0;
    if (now - sentAt[b] < (changed ? MIN_BLOCK_GAP_MS : KEEPALIVE_MS)) continue;
    struct_dmx_packet p;
    memset(&p, 0, sizeof(p));
    p.blockNumber = b;
    memcpy(p.dmxvalues, src, 128);
    p.data[0] = SIGNATURE_A;
    p.data[1] = SIGNATURE_B;
    p.data[2] = SIGNATURE_C;
    p.data[STREAM_KIND] = STREAM_MULTIFIXTURE;
    p.data[STREAM_MULTIFIXTURE_MARK] = STREAM_MULTIFIXTURE_MAGIC;
    tubeBroadcast(p);
    memcpy(sent[b], src, 128);
    sentValid[b] = true;
    sentAt[b] = now;
  }
}

}  // namespace

void artnetLoop(uint32_t now, const ArtnetConfig& c) {
  if (!c.enabled) {
    if (listening) {
      udp.stop();
      listening = false;
    }
    return;
  }
  if (!listening) {
    listening = udp.begin(ARTNET_PORT) == 1;
    if (!listening) return;
    memset(sentValid, 0, sizeof(sentValid));
  }
  for (uint8_t i = 0; i < MAX_PACKETS_PER_LOOP; i++) {
    const int size = udp.parsePacket();
    if (size <= 0) break;
    const int len = udp.read(rx, sizeof(rx));
    handlePacket(c, len, now);
  }
  if (lastDmxMs && now - lastDmxMs < FEED_MS && !foreignLive(now)) relay(now, c.blocks);

  if (now - secondStart >= 1000) {
    fps = framesInSecond > 255 ? 255 : framesInSecond;
    framesInSecond = 0;
    secondStart = now;
  }
}

ArtnetStatus artnetStatus(uint32_t now) {
  ArtnetStatus s;
  s.listening = listening;
  s.receiving = lastDmxMs && now - lastDmxMs < RECEIVING_MS;
  s.suspended = s.receiving && foreignLive(now);
  s.fps = s.receiving ? fps : 0;
  s.from = lastFrom;
  return s;
}
