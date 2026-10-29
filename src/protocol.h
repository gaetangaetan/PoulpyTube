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

// Mode autonome (passerelle PoulpyTube). Voir docs/ARCHITECTURE.md § 6.
//
// LOOK : data[4..9] MAC cible (FF… = par groupes), [10..11] masque de groupes (bit g = groupe g,
// 0..9), [12..21] look, [22..23] fondu ms, [24..25] version de l'état, [26] drapeaux LOOK_*.
#define LOOK_CMD           0xC0
#define LOOK_MASK          10
#define LOOK_DATA          12
#define LOOK_FADE          22
#define LOOK_VERSION       24
#define LOOK_FLAGS         26
#define LOOK_FLAG_RESET    0x02  // un look de groupe posé par l'utilisateur efface le look individuel

// GATEWAY_BEACON : data[4..9] MAC de la passerelle, [10..13] horloge ms, [14..15] version de
// l'état, [16] canal, [17] drapeaux BEACON_*, [18] délai de retour du live en s.
#define GATEWAY_BEACON_CMD 0xC1
#define BEACON_DESIGNATED  0x01
#define BEACON_STATUS_REQ  0x02  // les barres répondent tout de suite par TUBE_STATUS

// SET_ROLE : data[4..9] MAC cible, [10] rôle (0 = barre, 1 = passerelle désignée).
#define SET_ROLE_CMD       0xC2

// TUBE_STATUS (barre → passerelle, diffusé) : data[4..9] MAC, [10] groupe, [11..12] adresse,
// [13..14] LEDs, [15..16] limite W, [17..20] énergie mWh, [21] source (0 autonome, 1 live),
// [22] drapeaux STATUS_*, [23..32] look autonome, [33] longueur version, [34..] version.
#define TUBE_STATUS_CMD    0xC3
#define STATUS_INDIVIDUAL  0x01
#define STATUS_GATEWAY     0x02
#define STATUS_WIFI_SUM    50    // empreinte du Wi-Fi de la maison enregistré (0 = aucun)

// WIFI_CONFIG (passerelle → barre) : data[4..9] MAC cible ; dmxvalues[0..32] nom, [33..97] mot de passe.
// Chaque barre garde le Wi-Fi : celle qui devient passerelle sait rejoindre la maison.
#define WIFI_CONFIG_CMD    0xC4
#define WIFI_CONFIG_PASS   33

// SCENE_CHUNK (passerelle → toutes, en continu) : data[10] numéro du morceau de 128 octets,
// [11..12] taille du fichier de scènes ; dmxvalues = le morceau. Chaque barre garde une copie.
#define SCENE_CHUNK_CMD    0xC5

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
      || (c >= LOOK_CMD && c <= SCENE_CHUNK_CMD)
      || (c >= 0xE0 && c <= 0xEF) || c == RESET_ENERGY_CMD || c == SET_POWER_LIMIT_CMD;
}

#endif
