// Protocole ESP-NOW PoulpyTube (paquet, signature, codes de commande).
// Référence : poulpy-ecosystem/protocol/espnow.md
#ifndef POULPY_PROTOCOL_H
#define POULPY_PROTOCOL_H

#include <stdint.h>

// 512 canaux en 4 blocs de 128 (un paquet ESP-NOW fait au plus 250 octets).
typedef struct struct_dmx_packet {
  uint8_t blockNumber;     // 0..3 = bloc DMX, 255 = commande
  uint8_t dmxvalues[128];
  uint8_t data[100];       // signature en data[0..2], commande en data[3]
} struct_dmx_packet;

#define SIGNATURE_A 0x3C
#define SIGNATURE_B 0x61
#define SIGNATURE_C 0x05

#define CMD_BLOCK 255
// MAC cible en data[4..9], charge utile à partir de data[10].
#define CMD_TARGET_MAC 4
#define CMD_PAYLOAD 10

// Flux « multifixture » : slot de 10 canaux à l'adresse du tube.
#define STREAM_KIND 5
#define STREAM_MULTIFIXTURE 0x01
#define STREAM_MULTIFIXTURE_MARK 6
#define STREAM_MULTIFIXTURE_MAGIC 0xA5

// Groupes 0..9 sur le fil (1..10 à l'écran).
#define GROUP_COUNT 10

// Émetteur → tube
#define OTA_UPDATE_CMD          0xFA
#define REQUEST_MAC_CMD         0xFB
#define ENTER_REMOTE_SETUP_CMD  0xFD
#define EXIT_REMOTE_SETUP_CMD   0xFE
#define SET_GROUP_NUMBER_CMD    0xFF
#define PANIC_RESTART_CMD       0xF9
#define ANIMATION_TRIGGER_CMD   0xA0  // ancien ONIRIGUN, ignoré
#define GET_INFO_CMD            0xE0
#define GET_CHANNEL_CMD         0xE1
#define START_SCAN_CMD          0xE2  // retiré : répond « inconnue »
#define SET_CHANNEL_CMD         0xE3
#define SET_LED_COUNT_CMD       0xE4
#define START_STATS_CMD         0xE5
#define STOP_STATS_CMD          0xE6
#define SET_BRIGHTNESS_CMD      0xE7
#define RESTART_SINGLE_CMD      0xE8
#define SAVE_CONFIG_CMD         0xE9
#define IDENTIFY_CMD            0xEA
#define PING_CMD                0xEB
#define SET_STATS_SERIAL_CMD    0xEC
#define SET_DMX_ADDRESS_CMD     0xED
#define SET_MULTIFIXTURE_CMD    0xEE
#define GET_DMX_ADDRESS_CMD     0xEF
#define RESET_ENERGY_CMD        0xF0
#define SET_POWER_LIMIT_CMD     0xF1

// Tube → émetteur
#define RESPONSE_MAC_CMD         0xFC
#define RESPONSE_INFO_CMD        0xD0
#define RESPONSE_CHANNEL_CMD     0xD1
#define RESPONSE_STATS_CMD       0xD3
#define RESPONSE_ACK_CMD         0xD4
#define RESPONSE_PONG_CMD        0xD6
#define RESPONSE_DMX_ADDRESS_CMD 0xD7

// Statut des ACK
#define ACK_OK 0
#define ACK_ERROR 1
#define ACK_INVALID 2
#define ACK_UNKNOWN 255

inline bool isCommandCode(uint8_t c) {
  return c == OTA_UPDATE_CMD || c == REQUEST_MAC_CMD || c == RESPONSE_MAC_CMD
      || c == ENTER_REMOTE_SETUP_CMD || c == EXIT_REMOTE_SETUP_CMD
      || c == SET_GROUP_NUMBER_CMD || c == PANIC_RESTART_CMD
      || c == ANIMATION_TRIGGER_CMD
      || (c >= 0xE0 && c <= 0xEF) || c == RESET_ENERGY_CMD || c == SET_POWER_LIMIT_CMD;
}

#endif
