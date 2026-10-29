# PoulpyTube — architecture cible

> Statut : **validé le 2026-10-29** (adresse 0, canal 10 déclencheur, pas de mot de passe). midiPoulpy reporté : pas assez de retour d'usage pour figer ses effets. Rien n'est codé. Une fois validé, ce document devient la référence du firmware et des émetteurs (PoulpyNode, PoulpyLink, midiPoulpy, Pollux).

## 1. En une phrase

Une barre affiche toujours **un look**. Ce look vient soit du **live** (un flux continu envoyé par un émetteur), soit de son **look autonome** (mémorisé, posé depuis le téléphone). Le live a la priorité ; quand il se tait, la barre revient toute seule à son look autonome.

## 2. Le look

Un look = les 10 octets du slot PoulpyTube actuel (type Pollux `…00B1`), inchangés pour rester compatible :

| Canal | Rôle |
|---|---|
| 1–3 | Rouge, Vert, Bleu |
| 4 | Look (bandes de 32) : Uni, Rainbow, Sparkle, Flicker, Wipe, Breathe, Crossing, Chase |
| 5 | Vitesse |
| 6 | Largeur |
| 7 | Extra |
| 8 | Aléatoire |
| 9 | Extra 2 (aujourd'hui ignoré, sera rendu) |
| 10 | **Déclencheur** (proposition) : chaque changement de valeur relance l'animation depuis le début. Sert aux retriggers MIDI. |

Changements par rapport à aujourd'hui :

- Les animations sont calculées **d'après l'horloge** (millisecondes), plus d'après le nombre d'images. Une vitesse donne le même résultat quelle que soit la charge.
- Les animations utiles des anciens modes (comète, Knight Rider, vague, ripple, ping…) deviennent de nouveaux looks, dans des bandes encore libres ou à la place d'un look peu utilisé. La liste se décide après l'inventaire midiPoulpy (§ 9).

## 3. Les deux sources

### Live

- C'est le flux « multifixture » actuel : univers DMX en 4 blocs de 128, marqueur `data[5]=0x01` + `data[6]=0xA5`. La barre lit son slot à son adresse.
- Émetteurs : PoulpyNode et PoulpyLink (pilotés par Pollux), midiPoulpy (§ 9).
- Dès qu'un paquet live arrive, la barre le suit (pas de délai).

### Autonome

- La barre garde **son** look autonome en EEPROM : elle le joue au démarrage, même sans passerelle.
- Le look autonome est posé par la passerelle (commande `LOOK`, § 6), pour un groupe ou pour une barre précise.
- Quand le live se tait pendant le **délai de retour** (défaut 4 s, réglable), la barre revient à son look autonome avec un fondu d'environ 1 s. Aujourd'hui : 1 s puis noir.

## 4. Groupes et adresses

- **Groupes 1 à 10** affichés partout (0 à 9 sur le fil, comme aujourd'hui). Une seule limite dans tout le code. Les commandes de groupe utilisent un masque 16 bits, pour pouvoir monter à 16 plus tard sans changer le protocole.
- Le groupe est **enregistré tout de suite** quand il change (plus besoin de « sortir du setup » ou de `SAVE_CONFIG`).
- **Adresse 0 = « suit le groupe »** (proposition) : la barre lit alors le slot à l'adresse `1 + (groupe − 1) × 10`. Les 10 groupes occupent les 100 premiers canaux. Conséquence : un groupe et une zone live deviennent la même chose, sans rien régler. Une adresse explicite (Pollux) reste prioritaire.
- Adresse maximale : **503** (slot de 10 canaux dans 512). La doc dira 503 partout.
- Un slot peut chevaucher deux blocs de 128 : la barre attend d'avoir les deux blocs de la même trame avant d'appliquer.

## 5. La passerelle

N'importe quelle barre peut être passerelle. Elle continue d'afficher son propre ruban.

### Qui est passerelle

- Le rôle se pose depuis Pollux (page Lampes) ou depuis l'application web d'une passerelle existante. Il est enregistré en EEPROM.
- **Secours automatique** : une barre qui n'entend aucune passerelle pendant 30 s devient passerelle.
- **Une seule passerelle** : chaque passerelle émet une balise (§ 6). Une passerelle qui entend la balise d'une passerelle à **plus petite MAC** redevient une simple barre. Plusieurs barres allumées sans passerelle se départagent ainsi toutes seules, en quelques secondes.
- Une passerelle désignée explicitement ne se retire que face à une autre passerelle désignée explicitement (la règle de la plus petite MAC s'applique alors aussi). Une passerelle de secours se retire toujours devant une passerelle désignée.

### Wi-Fi

- **Par défaut : le Wi-Fi de la maison** (SSID et mot de passe enregistrés depuis l'application).
- **Secours** : si la connexion échoue (pas de réglage, ou 20 s sans succès), la passerelle ouvre son propre Wi-Fi `PoulpyTube-xxxx`, ouvert (sans mot de passe). Elle retente la maison toutes les 2 minutes tant que personne n'est connecté à son Wi-Fi.
- **Adresse** : `poulpytube.local`.
  - Sur son propre Wi-Fi : la passerelle répond elle-même aux noms (DNS local). Toute adresse mène à la page, qui s'ouvre souvent toute seule à la connexion.
  - Sur la maison : mDNS. Fonctionne sur iPhone et la plupart des Android récents ; l'IP est affichée en secours dans l'application et sur le port série.
- **Canal radio** : ESP-NOW doit être sur le canal du Wi-Fi. Sur la maison, le canal est celui du routeur : la passerelle le diffuse aux barres (`SET_CHANNEL`, comme le hop de PoulpyLink). Sur son propre Wi-Fi, elle garde le canal enregistré.

### Ce qu'elle fait

- Garde la table **groupe → look** (et les looks posés barre par barre), en EEPROM.
- Sur une demande du téléphone : envoie la commande `LOOK` tout de suite, répétée 3 fois à ~30 ms d'écart.
- **Rediffusion** toutes les 1,5 s : balise + état complet. Une barre allumée en retard ou qui a raté un paquet se met à jour en moins de 2 s.
- Garde la liste des barres entendues (MAC, groupe, adresse, LEDs, firmware, batterie), mise à jour par les réponses aux scans.

## 6. Protocole radio

Paquet inchangé (`struct_dmx_packet`, 229 octets, signature `3C 61 05`, commandes `blockNumber = 255`, code en `data[3]`, MAC cible en `data[4..9]`, charge utile dès `data[10]`). Toutes les commandes existantes restent. Trois commandes nouvelles :

| Code | Nom | Charge utile (`data[10…]`) |
|---|---|---|
| `0xC0` | `LOOK` | masque de groupes (2 octets, `0xFFFF` = toutes) ou MAC cible en `data[4..9]` ; look (10 octets) ; fondu en ms (2 octets) ; numéro de version de l'état (2 octets) ; drapeau « mémoriser » |
| `0xC1` | `GATEWAY_BEACON` | horloge de la passerelle en ms (4 octets) ; version de l'état (2 octets) ; canal ; drapeaux (désignée / secours) |
| `0xC2` | `SET_ROLE` | MAC cible ; rôle (0 = barre, 1 = passerelle) ; délai de retour du live en s |

- Une barre ignore un `LOOK` dont la version est déjà appliquée : les répétitions et rediffusions ne coûtent rien.
- Les barres calent leur horloge d'animation sur la balise : les looks qui traversent plusieurs barres restent synchrones à quelques ms.
- Les constantes viennent de l'en-tête partagé `espnow_protocol.h`, inclus par le firmware (plus de copies).

## 7. Application web (téléphone)

### Principes

- Servie par la passerelle à `poulpytube.local`. Pas d'installation.
- Téléphone en portrait, épurée, gros boutons et faders tactiles.
- **Pas de défilement** : onglets en bas, panneaux qui s'ouvrent par-dessus. Seules les listes longues (beaucoup de barres) défilent.
- Compilée dans le firmware (compressée) : un seul `.bin`, l'OTA par catalogue ne change pas.
- HTML + JavaScript simples, sans framework, quelques dizaines de Ko.

### Onglets (proposition, à valider sur maquette)

1. **Jouer** : les groupes en grandes tuiles (couleur du look actuel). Toucher un groupe ouvre un panneau : couleurs rapides, look, vitesse, largeur. Une tuile « Toutes ».
2. **Barres** : liste des barres trouvées. Toucher une barre : clignoter, groupe, adresse, LEDs, limite en watts, batterie, look individuel.
3. **Réglages** : Wi-Fi de la maison, nom, délai de retour du live, canal, passerelle, mise à jour du firmware des barres.

Plus tard, si utile : des scènes complètes (« groupe 1 bleu, groupe 2 chase rouge ») rappelées d'un geste.

### API (passerelle)

JSON sur HTTP, réponses immédiates : une requête ne fait qu'enregistrer la demande, la boucle principale l'exécute.

| Méthode | Chemin | Rôle |
|---|---|---|
| GET | `/api/state` | groupes, looks, passerelle, Wi-Fi, version |
| POST | `/api/look` | `{ "groups": [1,2] \| "mac": "…", "look": [10 octets], "fadeMs": 500 }` |
| GET | `/api/tubes` | barres entendues |
| POST | `/api/scan` | relance un scan |
| POST | `/api/tube` | `{ "mac": "…", "group" \| "address" \| "leds" \| "wattLimit" \| "identify" }` |
| POST | `/api/settings` | Wi-Fi, délai de retour, canal, nom |

Même API envisageable plus tard sur PoulpyNode.

## 8. Émetteurs

| Émetteur | Source | Envoie |
|---|---|---|
| PoulpyNode (Pollux) | live | flux multifixture, commandes de setup |
| PoulpyLink (Pollux) | live | idem par USB |
| midiPoulpy | live | flux multifixture depuis le MIDI (§ 9) |
| Passerelle PoulpyTube | autonome | `LOOK`, balise, commandes de setup |

PoulpyLights ne pilote plus les barres. Ses anciennes cartes recevront un firmware compatible PoulpyNode.

## 9. midiPoulpy

### Aujourd'hui (inventaire du dépôt `midiPoulpyLights`)

- Envoie l'ancien flux « mode » : canal 1 = mode **6** (« RVB par groupe »), puis 3 canaux RVB par groupe pour les 10 groupes.
- Notes : l'octave choisit le groupe (2 à 11 → groupes 1 à 10), le degré choisit une couleur de la palette active, la vélocité donne l'intensité. Note 1 = choix de la palette (128 palettes de 12 couleurs).
- Notes C0–B0 = **12 effets momentanés** : chacun est un buffer complet d'ancien mode (n'importe quel mode, choisi dans le Lab). Pour le mode 80, la vélocité module le canal 9.
- Fondus entre couleurs de groupe calculés par midiPoulpy.
- Gestion des barres (scan, groupe, LEDs, identifier, OTA, hop de canal) : déjà la même chose que PoulpyLink.

### Cible

- midiPoulpy devient un « PoulpyLink à entrée MIDI » : il envoie le **flux multifixture**, un slot par groupe, à l'adresse « suit le groupe » (§ 4). Les barres n'ont rien à régler.
- Couleurs de groupe : slot avec look Uni + RVB. Mêmes palettes, mêmes notes, même ressenti.
- Effets : un effet devient un jeu de looks par groupe. Les effets « entre groupes » (chase de groupes, comète, Knight Rider, ripple…) sont soit calculés par midiPoulpy à 40 images/s (un ESP32 le fait sans peine), soit joués par les barres avec l'horloge commune.
- Retriggers : canal 10 (déclencheur).
- Le Lab garde son rôle (palettes, effets), avec un aperçu basé sur les nouveaux looks.

### Transition

- Reportée : midiPoulpy a servi pour un concert, l'usage n'est pas assez établi pour figer ses effets.
- Les anciens modes restent dans le firmware des barres (module `legacy`) jusqu'à ce que midiPoulpy soit adapté, puis sont retirés.

## 10. Organisation du firmware

### Règles

- La fonction de réception radio **ne fait que mettre le paquet en file**. Tout le traitement se fait dans la boucle.
- Aucun `delay()` hors démarrage. Scan, redémarrage, OTA, hop de canal = machines d'état sur `millis()`.
- Le serveur web suit la même règle : une requête enregistre la demande, la boucle l'exécute.
- FastLED déclaré avec le **nombre réel de LED** (plus 300 fixes).
- Configuration = **une structure** en EEPROM, avec version et CRC. Lecture de l'ancienne disposition au premier démarrage (migration), puis plus jamais.
- Version du firmware : ne change que quand on publie, pas à chaque compilation.

### Modules

| Module | Rôle |
|---|---|
| `radio` | ESP-NOW, file de réception, envoi |
| `protocol` | décodage des paquets (en-tête partagé) |
| `config` | structure EEPROM, version, CRC, migration |
| `looks` | moteur de looks (horloge, 10 canaux) |
| `source` | arbitrage live / autonome, délai de retour, fondus |
| `legacy` | anciens modes (temporaire, § 9) |
| `power` | limite en watts, compteur d'énergie |
| `ota` | mise à jour par catalogue |
| `gateway` | rôle, élection, Wi-Fi, table des looks, rediffusion |
| `web` | serveur HTTP, page compressée, API |

## 11. Réactivité attendue

| Quoi | Ordre de grandeur |
|---|---|
| Animation sur une barre | 60 à 100 images/s (calcul local) |
| Envoi aux LED (144) | ~4 ms |
| Geste sur le téléphone → lumière | 0,1 à 0,3 s |
| Barre en retard ou paquet perdu | rattrapé en moins de 2 s |
| Live → autonome | délai de retour (4 s) + fondu 1 s |
| Autonome → live | immédiat |

## 12. Décisions et points ouverts

- Canal 10 « déclencheur » : **validé**.
- Adresse 0 « suit le groupe » : **validée**.
- Wi-Fi de secours et application : **sans mot de passe**.
- Nouveaux looks : la liste se fera à l'usage. midiPoulpy n'est pas prioritaire ; ses effets ne seront pas inventoriés pour l'instant, et les anciens modes restent dans le firmware tant qu'il n'est pas adapté.
- L'OTA n'est pas authentifiée (inchangé pour l'instant).

## 13. Étapes

1. Relecture de ce document.
2. Maquette cliquable de l'application web, testée sur téléphone.
3. Nettoyage du firmware à comportement égal (§ 10). Testé sur deux Wemos, publié.
4. Mode autonome, passerelle, application web. Testé, publié.
5. midiPoulpy adapté, retrait des anciens modes.
6. Ajustements Pollux / PoulpyNode (rôle passerelle sur la page Lampes, adresse 0).
