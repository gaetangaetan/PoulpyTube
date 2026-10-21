#pragma once
// Le récepteur historique parle l'API ESP-NOW de l'ESP8266.
// Ici on la rejoue sur l'ESP32, pour le même source.

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

inline void wifi_set_channel(uint8_t channel) {
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

inline uint8_t wifi_get_channel() {
  uint8_t primary = 1;
  wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
  if (esp_wifi_get_channel(&primary, &second) != ESP_OK) return 1;
  return primary;
}

#ifndef ESP_NOW_ROLE_COMBO
#define ESP_NOW_ROLE_COMBO 0
#endif
#define esp_now_set_self_role(role) ((void)(role))

using PoulpyRecv8266 = void (*)(uint8_t* mac, uint8_t* data, uint8_t len);

static PoulpyRecv8266 poulpyRecv8266 = nullptr;
static esp_err_t (*poulpyRealAddPeer)(const esp_now_peer_info_t*) = ::esp_now_add_peer;
static esp_err_t (*poulpyRealRegRecv)(esp_now_recv_cb_t) = ::esp_now_register_recv_cb;

static void poulpyRecvThunk(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!poulpyRecv8266 || !info || !info->src_addr || !data || len <= 0) return;
  uint8_t mac[6];
  memcpy(mac, info->src_addr, 6);
  uint8_t n = len > 255 ? 255 : (uint8_t)len;
  poulpyRecv8266(mac, const_cast<uint8_t*>(data), n);
}

inline int espnow_register_recv_compat(PoulpyRecv8266 cb) {
  poulpyRecv8266 = cb;
  return poulpyRealRegRecv(poulpyRecvThunk) == ESP_OK ? 0 : 1;
}
#define esp_now_register_recv_cb espnow_register_recv_compat

inline int espnow_add_peer_compat(uint8_t* mac, uint8_t role, uint8_t channel, uint8_t* key, uint8_t key_len) {
  (void)role;
  (void)key;
  (void)key_len;
  if (!mac) return 1;
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel;
  peer.encrypt = false;
  peer.ifidx = WIFI_IF_STA;
  return poulpyRealAddPeer(&peer) == ESP_OK ? 0 : 1;
}
#define esp_now_add_peer espnow_add_peer_compat
