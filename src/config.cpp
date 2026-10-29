#include "config.h"

#include <Arduino.h>
#include <EEPROM.h>

#include "multifixure.h"
#include "protocol.h"

#define EEPROM_SIZE 256

#define ADDR_ADDRESS 0
#define ADDR_GROUP 8
#define ADDR_OTA 12
#define ADDR_CHANNEL 98
#define ADDR_ADDRESS_INIT 99
#define ADDR_LED_COUNT 100
#define ADDR_OTA_URL 102

#define ADDRESS_INIT_MAGIC 0xA7
#define OTA_URL_MAX 140
#define MAX_ADDRESS (512 - MULTIFIXTURE_SLOT + 1)

static void write16(int at, uint16_t v) {
  EEPROM.write(at, v & 0xFF);
  EEPROM.write(at + 1, (v >> 8) & 0xFF);
}

static uint16_t read16(int at) {
  return EEPROM.read(at) | (EEPROM.read(at + 1) << 8);
}

void configBegin() { EEPROM.begin(EEPROM_SIZE); }

uint8_t configLoadChannel() {
  uint8_t ch = EEPROM.read(ADDR_CHANNEL);
  return (ch >= 1 && ch <= 13) ? ch : 0;
}

void configSaveChannel(uint8_t channel) {
  if (channel < 1 || channel > 13) return;
  EEPROM.write(ADDR_CHANNEL, channel);
  EEPROM.commit();
}

uint16_t configLoadLedCount() {
  uint16_t n = read16(ADDR_LED_COUNT);
  return (n > 0 && n <= MAXLEDLENGTH) ? n : DEFAULT_LED_COUNT;
}

void configSaveLedCount(uint16_t count) {
  write16(ADDR_LED_COUNT, count);
  EEPROM.commit();
}

uint8_t configLoadGroup() {
  uint8_t g = EEPROM.read(ADDR_GROUP);
  return g < GROUP_COUNT ? g : 0;
}

void configSaveGroup(uint8_t group) {
  if (EEPROM.read(ADDR_GROUP) == group) return;
  EEPROM.write(ADDR_GROUP, group);
  EEPROM.commit();
}

int configLoadAddress(uint8_t group) {
  if (EEPROM.read(ADDR_ADDRESS_INIT) != ADDRESS_INIT_MAGIC) {
    int def = 1 + MULTIFIXTURE_SLOT * group;
    if (def > MAX_ADDRESS) def = 1;
    configSaveAddress(def);
    return def;
  }
  int a = read16(ADDR_ADDRESS);
  return (a >= 1 && a <= MAX_ADDRESS) ? a : 1;
}

void configSaveAddress(int address) {
  write16(ADDR_ADDRESS, (uint16_t)address);
  EEPROM.write(ADDR_ADDRESS_INIT, ADDRESS_INIT_MAGIC);
  EEPROM.commit();
}

void configSaveAll(uint8_t group, uint8_t channel, uint16_t ledCount) {
  EEPROM.write(ADDR_GROUP, group);
  if (channel >= 1 && channel <= 13) EEPROM.write(ADDR_CHANNEL, channel);
  write16(ADDR_LED_COUNT, ledCount);
  EEPROM.commit();
}

void configSaveOta(const char* ssid, const char* password, const char* url) {
  uint8_t ssidLen = ssid ? min((size_t)32, strlen(ssid)) : 0;
  uint8_t passLen = password ? min((size_t)63, strlen(password)) : 0;
  uint8_t urlLen = url ? min((size_t)OTA_URL_MAX, strlen(url)) : 0;

  EEPROM.write(ADDR_OTA, 1);
  EEPROM.write(ADDR_OTA + 1, ssidLen);
  EEPROM.write(ADDR_OTA + 2, passLen);
  for (int i = 0; i < ssidLen; i++) EEPROM.write(ADDR_OTA + 3 + i, ssid[i]);
  for (int i = 0; i < passLen; i++) EEPROM.write(ADDR_OTA + 35 + i, password[i]);
  EEPROM.write(ADDR_OTA_URL, urlLen);
  for (int i = 0; i < urlLen; i++) EEPROM.write(ADDR_OTA_URL + 1 + i, url[i]);
  EEPROM.commit();
}

// ssid ≥ 33, password ≥ 64, url ≥ 141 octets.
bool configLoadOta(char* ssid, char* password, char* url) {
  if (EEPROM.read(ADDR_OTA) != 1) return false;
  uint8_t ssidLen = min(32, (int)EEPROM.read(ADDR_OTA + 1));
  uint8_t passLen = min(63, (int)EEPROM.read(ADDR_OTA + 2));
  for (int i = 0; i < ssidLen; i++) ssid[i] = EEPROM.read(ADDR_OTA + 3 + i);
  ssid[ssidLen] = '\0';
  for (int i = 0; i < passLen; i++) password[i] = EEPROM.read(ADDR_OTA + 35 + i);
  password[passLen] = '\0';
  uint8_t urlLen = EEPROM.read(ADDR_OTA_URL);
  if (urlLen > OTA_URL_MAX) urlLen = 0;
  for (int i = 0; i < urlLen; i++) url[i] = EEPROM.read(ADDR_OTA_URL + 1 + i);
  url[urlLen] = '\0';
  return true;
}

void configClearOta() {
  EEPROM.write(ADDR_OTA, 0);
  EEPROM.write(ADDR_OTA_URL, 0);
  EEPROM.commit();
}
