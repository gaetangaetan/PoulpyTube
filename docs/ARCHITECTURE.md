# PoulpyLED (ex-PoulpyTube) — architecture cible

> Statut : **validé le 2026-10-29** (adresse 0, canal 10 déclencheur, pas de mot de passe). midiPoulpy reporté : pas assez de retour d'usage pour figer ses effets. Rien n'est codé. Une fois validé, ce document devient la référence du firmware et des émetteurs (PoulpyNode, PoulpyLink, midiPoulpy, Pollux).

## 1. En une phrase

Une barre affiche toujours **un look**. Ce look vient soit du **live** (un flux continu envoyé par un émetteur), soit de son **look autonome** (mémorisé, posé depuis le téléphone). Le live a la priorité ; quand il se tait, la barre revient toute seule à son look autonome.

## 2. Le look

Un look = les 10 octets du slot PoulpyLED actuel (type Pollux `…00B1`), inchangés pour rester compatible :

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

Sens réel des canaux 5 à 8 selon le look (`renderLook`), tel que l'app web les nomme :

| Look | 5 | 6 | 7 | 8 |
|---|---|---|---|---|
| Uni | – | – | – | – |
| Arc-en-ciel | Sens (128–148 = arrêt) | Étalement | – | – |
| Paillettes | Fréquence | Densité | (fréquence en plus, laissé à 0) | – |
| Flicker | Éclairs blancs | Fond (niveau éteint) | Allumé (part du temps) | – |
| Wipe | Vitesse | Longueur | – | Variation |
| Respire | Vitesse | Plancher | (phase, laissé à 0) | – |
| Croisement | Vitesse | Longueur | 2e couleur (teinte FastLED, 0 = blanc) | Variation |
| Chase | Vitesse | Longueur | Écart | Variation |

L'app n'affiche que les réglages du look choisi. Changer de look remet ses valeurs par défaut et met à 0 les canaux qu'il n'utilise pas. Chaque look a des presets (`LOOK_PRESETS` dans `web/index.html`), affichés avec un aperçu animé qui reprend le calcul du firmware.

Changements par rapport à aujourd'hui :

- Les animations sont calculées **d'après l'horloge** (millisecondes), plus d'après le nombre d'images. Une vitesse donne le même résultat quelle que soit la charge.
- Les animations utiles des anciens modes (comète, Knight Rider, vague, ripple, ping…) deviennent de nouveaux looks, dans des bandes encore libres ou à la place d'un look peu utilisé. La liste se décide après l'inventaire midiPoulpy (§ 9).

## 3. Les deux sources

### Live

- C'est le flux « multifixture » actuel : univers DMX en 4 blocs de 128, marqueur `data[5]=0x01` + `data[6]=0xA5`. La barre lit son slot à son adresse.
- Émetteurs : PoulpyNode et PoulpyLink (pilotés par Pollux), midiPoulpy (§ 9), et la passerelle elle-même quand son **nœud Art-Net** est actif (ci-dessous).
- Dès qu'un paquet live arrive, la barre le suit (pas de délai).

### Nœud Art-Net de la passerelle

- Réglages → **Nœud Art-Net** : Coupé / Actif + univers (numéroté comme dans Pollux, à partir de 0). Coupé par défaut. Enregistré dans les réglages (v3) et **partagé par la balise** : la barre qui prend le relais de passerelle reprend le nœud.
- Actif : la passerelle écoute UDP 6454 sur son Wi-Fi (maison ou point d'accès `PoulpyLED`, alors `192.168.4.1`). Elle répond à ArtPoll (nom court `PoulpyLED`) : Pollux la voit dans ses nœuds Art-Net.
- Un ArtDmx de son univers est appliqué tout de suite à son propre slot, puis relayé en flux multifixture (même paquet que PoulpyNode). Seuls les blocs de 128 où une barre connue lit son slot partent. Un bloc part quand il change (50 images/s au plus), sinon toutes les 250 ms. Plus d'Art-Net depuis 0,5 s : plus rien ne part, les barres comptent leur délai de retour.
- **Un seul émetteur** : si la passerelle entend un flux live radio (PoulpyNode, PoulpyLink, midiPoulpy) depuis moins de 1,5 s, elle n'applique ni ne relaie son Art-Net. L'application l'affiche (« suspendu »).
- Viser la passerelle en **unicast** : le broadcast Art-Net passe au débit de base du Wi-Fi et sature vite. Comme pour le Wi-Fi de la maison, la passerelle ne le rejoint que si le routeur est sur le canal des barres.
- L'IP change avec la passerelle : après une passation, relancer la recherche de nœuds dans Pollux.

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
- **Secours automatique** : une barre qui n'entend aucune passerelle pendant 8 s (plus 0 à 7,5 s selon sa MAC) devient passerelle. Mesuré : ~12 s après la disparition de la passerelle.
- **Passation** : désigner une autre barre depuis l'application. L'ancienne cesse aussitôt de se dire désignée, puis redémarre ; la nouvelle devient passerelle sans redémarrer. Mesuré : 7 à 8 s. Une passerelle désignée qui revient reprend la main en ~2 s.
- **Une seule passerelle** : chaque passerelle émet une balise (§ 6). Une passerelle qui entend la balise d'une passerelle à **plus petite MAC** redevient une simple barre. Plusieurs barres allumées sans passerelle se départagent ainsi toutes seules, en quelques secondes.
- Une passerelle désignée explicitement ne se retire que face à une autre passerelle désignée explicitement (la règle de la plus petite MAC s'applique alors aussi). Une passerelle de secours se retire toujours devant une passerelle désignée.

### Wi-Fi

- **Par défaut : le Wi-Fi de la maison** (SSID et mot de passe enregistrés depuis l'application, ou repris de la dernière mise à jour OTA envoyée par Pollux si la barre n'en a pas). Un seul Wi-Fi **actif** à la fois ; jusqu'à 8 réseaux **mémorisés** (LittleFS `/wifi.json`) pour un rappel rapide dans l'UI. **Réinitialiser (tests)** : `POST /api/settings` `{ "resetWifi": true }` efface le Wi-Fi actif et la liste mémorisée sur la passerelle et diffuse un `WIFI_CONFIG` vide à toutes les barres ; la passerelle rouvre `PoulpyLED`.
- **Partagé** : la passerelle donne le Wi-Fi actif à toutes les barres (`WIFI_CONFIG`, seulement à celles dont l'empreinte dans `TUBE_STATUS` diffère). Chacune le garde en EEPROM : la barre qui devient passerelle rejoint la maison toute seule.
- **Secours** : si la maison n'est pas joignable (pas de réglage, introuvable, autre canal, ou 20 s sans succès), la passerelle ouvre le Wi-Fi `PoulpyLED`, ouvert. Même nom pour toutes les barres : le téléphone ne voit qu'un réseau. Elle retente la maison toutes les 2 minutes ; connectée, elle coupe ce point d'accès.
- **Adresse** : `poulpyled.local` (avant le 2026-11-02 : `poulpytube.local`). L'ESP8266 n'annonce qu'un nom en mDNS ; sur le point d'accès, le DNS local répond à tous les noms.
  - Sur son propre Wi-Fi : la passerelle répond elle-même aux noms (DNS local), à `192.168.4.1`. Toute adresse mène à la page, qui s'ouvre souvent toute seule à la connexion.
  - Sur la maison : mDNS. L'IP change avec la passerelle : la page ouverte se reconnecte et se recharge toute seule.
- **Canal radio** : une barre n'a qu'une radio. La passerelle ne rejoint la maison **que si le routeur est sur le canal des barres** ; elle ne change jamais de canal d'elle-même (les barres quitteraient PoulpyNode / PoulpyLink). L'application montre le canal du routeur et propose de passer toutes les barres dessus ; PoulpyNode doit suivre.

### Ce qu'elle fait

- Garde la table **groupe → look** (et les looks posés barre par barre) et les **20 scènes**, en flash (LittleFS).
- Diffuse en continu le fichier des scènes (`SCENE_CHUNK`, un morceau de 128 octets toutes les 150 ms, ~10 s par tour). Chaque barre en garde une copie : une nouvelle passerelle a déjà les scènes. Une nouvelle passerelle apprend les looks en cours des statuts des barres.
- Sur une demande du téléphone : envoie la commande `LOOK` tout de suite, répétée 3 fois à ~30 ms d'écart.
- **Rediffusion** toutes les 1,5 s : balise + état complet. Une barre allumée en retard ou qui a raté un paquet se met à jour en moins de 2 s.
- Garde la liste des barres entendues (MAC, groupe, adresse, LEDs, firmware, batterie), mise à jour par les réponses aux scans.

## 6. Protocole radio

Paquet inchangé (`struct_dmx_packet`, 229 octets, signature `3C 61 05`, commandes `blockNumber = 255`, code en `data[3]`, MAC cible en `data[4..9]`, charge utile dès `data[10]`). Toutes les commandes existantes restent. Sept commandes nouvelles :

| Code | Nom | Charge utile (`data[10…]`) |
|---|---|---|
| `0xC0` | `LOOK` | masque de groupes (2 octets, `0xFFFF` = toutes) ou MAC cible en `data[4..9]` ; look (10 octets) ; fondu en ms (2 octets) ; numéro de version de l'état (2 octets) ; drapeau « mémoriser » |
| `0xC1` | `GATEWAY_BEACON` | horloge de la passerelle en ms (4 octets) ; version de l'état (2 octets) ; canal ; drapeaux (désignée / secours) ; `data[18]` délai de retour ; `data[19]` nœud Art-Net (0/1) ; `data[20..21]` univers |
| `0xC2` | `SET_ROLE` | MAC cible ; rôle (0 = barre, 1 = passerelle) ; délai de retour du live en s |
| `0xC3` | `TUBE_STATUS` | barre → passerelle : groupe, adresse, LEDs, limite W, mWh, source, drapeaux, look autonome, firmware ; `data[50]` empreinte du Wi-Fi ; `data[51]` longueur du nom, `data[52…]` nom (≤ 24) |
| `0xC4` | `WIFI_CONFIG` | MAC cible ; `dmxvalues[0..32]` SSID, `[33..97]` mot de passe |
| `0xC5` | `SCENE_CHUNK` | numéro du morceau ; taille du fichier (2 octets) ; `dmxvalues` = 128 octets du fichier des scènes |
| `0xC6` | `SET_NAME` | MAC cible ; `dmxvalues[0..24]` nom (EEPROM, suit la barre) |

Détail octet par octet : `src/protocol.h`. PoulpyNode ignore ces commandes.

- Une barre ignore un `LOOK` dont la version est déjà appliquée : les répétitions et rediffusions ne coûtent rien.
- Les barres calent leur horloge d'animation sur la balise : les looks qui traversent plusieurs barres restent synchrones à quelques ms.
- Les constantes viennent de l'en-tête partagé `espnow_protocol.h`, inclus par le firmware (plus de copies).

## 7. Application web (téléphone)

### Principes

- Servie par la passerelle à `poulpyled.local`. Pas d'installation.
- Téléphone en portrait, épurée, gros boutons et faders tactiles.
- **Pas de défilement** : onglets en bas, panneaux qui s'ouvrent par-dessus. Seules les listes longues (beaucoup de barres) défilent.
- Compilée dans le firmware (compressée) : un seul `.bin`, l'OTA par catalogue ne change pas.
- HTML + JavaScript simples, sans framework, quelques dizaines de Ko.

### Onglets (maquette validée : `web/index.html`)

1. **Jouer** : les groupes en grandes tuiles avec aperçu animé, plus une tuile « Toutes ». Toucher un groupe ouvre un panneau : 12 couleurs, teinte, intensité, look, vitesse, largeur.
2. **Scènes** : 20 emplacements. Toucher = rappeler (ou enregistrer si vide), appui long = renommer, fondu, remplacer, effacer.
3. **Barres** : liste des barres trouvées (nom si donné, sinon fin de MAC). Toucher = réglages (nom, clignoter, groupe, adresse, LEDs, limite, look, passerelle). Appui long (~1 s) = faire clignoter sans ouvrir le panneau.
4. **Réglages** : Wi-Fi de la maison (scan des SSID, liste des réseaux mémorisés, un Wi-Fi actif partagé aux barres), passerelle, délai de retour du live, canal, sauvegarde, **mises à jour** (catalogue + OTA depuis le téléphone, même flux `0xFA` que Pollux / PoulpyNode).

### Scènes

- Une scène = nom, fondu au rappel, le look de chaque groupe, et les looks des barres réglées seules (par MAC).
- **20 scènes, stockées sur la passerelle** (fichier en flash, quelques Ko). Tous les téléphones voient les mêmes ; rien n'est gardé dans le téléphone.
- Rappel : la passerelle envoie un `LOOK` par groupe puis par barre réglée seule, avec le fondu de la scène. Elle applique la même règle que pour un look : poser un groupe efface les looks individuels de ses barres.
- La passerelle mémorise la dernière scène rappelée et la rejoue au démarrage (c'est le look autonome des barres).

### Sauvegarde (gaetanstreel.com)

- **Une seule sauvegarde** : `https://poulpylights.gaetanstreel.com/poulpytube/api/backup.php` (source : `server/api/backup.php`, envoi : `python server/deploy.py`). Le serveur garde aussi les 30 versions précédentes.
- C'est le **téléphone** qui parle au serveur, pas la passerelle (un ESP8266 en HTTPS est lourd). Il faut donc que le téléphone ait internet, en pratique sur le Wi-Fi de la maison.
- Contenu (`format: poulpytube.backup`, `version: 1`) : les scènes, les réglages des barres par MAC (groupe, adresse, LEDs, limite), le délai de retour du live.
- Récupérer remplace les scènes ; les réglages des barres ne sont repris que si on coche la case.
- Exporter / importer un fichier JSON depuis le téléphone, sans internet.
- Pas de mot de passe (comme l'API de scènes du Lab midiPoulpy) : quelqu'un qui connaît l'adresse peut écraser la sauvegarde, d'où les 30 versions gardées.

### API (passerelle)

JSON sur HTTP, réponses immédiates : une requête ne fait qu'enregistrer la demande, la boucle principale l'exécute.

| Méthode | Chemin | Rôle |
|---|---|---|
| GET | `/api/state` | `product: "PoulpyLED"`, groupes, looks, passerelle, Wi-Fi, version, `artnet` `{ on, universe, listening, receiving, suspended, fps, from }` (`universe` = numéro Art-Net ; l'app l'affiche + 1, comme le PoulpyNode) |
| POST | `/api/look` | `{ "groups": [1,2] \| "mac": "…", "look": [10 octets], "fadeMs": 500 }` |
| GET | `/api/tubes` | barres entendues |
| POST | `/api/scan` | relance un scan |
| POST | `/api/tube` | `{ "mac": "…", "name" \| "group" \| "address" \| "leds" \| "wattLimit" \| "identify" \| "resetEnergy" }` |
| GET | `/api/wifi/scan` | asynchrone : `{ "scanning": true }` puis `{ "scanning": false, "networks": [{ssid,rssi,channel,open}] }` |
| GET | `/api/wifi/known` | réseaux mémorisés `[{ssid,pass}]` (≤ 8) |
| GET | `/api/firmware/catalog` | catalogue HTTP (passerelle sur le Wi-Fi maison) : `{ ok, releases:[{version,date,note,url}] }` |
| POST | `/api/ota` | `{ "url", "mac"? }` — OTA `0xFA` (SSID/pass = Wi-Fi maison) ; sans `mac` = toutes + passerelle |
| POST | `/api/settings` | Wi-Fi actif, `forgetWifi`, `resetWifi` (tests : oublier Wi-Fi sur toutes les barres), délai de retour, canal, `artnet` (bool), `artnetUniverse` (0..32767) |
| GET | `/api/scenes` | les scènes enregistrées |
| POST | `/api/scenes` | `{ "slot", "name", "fadeMs" }` : enregistre l'état actuel |
| POST | `/api/scenes/meta` | renomme ou change le fondu |
| POST | `/api/scenes/recall` | `{ "slot" }` |
| POST | `/api/scenes/delete` | `{ "slot" }` |
| GET | `/api/backup` | tout (format `poulpytube.backup`) |
| POST | `/api/backup` | `{ "backup", "tubes": true \| false }` : restaure |

Pollux (page Lampes) s'en sert quand ni PoulpyNode ni PoulpyLink ne répond : il trouve la passerelle (`poulpyled.local`, dernière IP, ArtPoll « PoulpyLED », `192.168.4.1`), lit `/api/tubes` (noms compris), écrit nom / groupe / adresse / LEDs / limite / `resetEnergy` / `identify` par `/api/tube`, passe l'OTA par `/api/ota`, et règle `artnet` + `artnetUniverse` sur l'univers de ses fixtures PoulpyLED (puis envoie ce flux à l'IP de la passerelle si elle ne le reçoit pas déjà).

## 8. Émetteurs

| Émetteur | Source | Envoie |
|---|---|---|
| PoulpyNode (Pollux) | live | flux multifixture, commandes de setup |
| PoulpyLink (Pollux) | live | idem par USB |
| midiPoulpy | live | flux multifixture depuis le MIDI (§ 9) |
| Passerelle PoulpyLED | autonome | `LOOK`, balise, commandes de setup |
| Passerelle PoulpyLED, nœud Art-Net | live | flux multifixture depuis l'Art-Net reçu sur son Wi-Fi |

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

### Publier une version

1. `POULPY_RELEASE=1` puis `pio run -e d1_mini` (PowerShell : `$env:POULPY_RELEASE="1"; pio run -e d1_mini`). Sans la variable, `build_version.h` et `data/version.txt` ne bougent pas.
2. Tester ce binaire. Les barres sont fermées : passer par l'OTA du PoulpyNode (`POST /api/tubes/ota` avec `mac`, `ssid`, `pass`, `url`) en servant le fichier depuis le PC, et vérifier qu'une deuxième OTA passe encore.
3. Copier `.pio/build/d1_mini/firmware.bin` et `data/version.txt` dans `poulpylights_v3.0/mrLEDTUBE-receiver` (mêmes chemins), puis `python scripts/publish_release.py --receiver-only --no-build --note "…"` depuis `poulpylights_v3.0`.

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
| `artnet` | nœud Art-Net de la passerelle : ArtPoll, ArtDmx, relais multifixture |
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
3. Nettoyage du firmware à comportement égal (§ 10). Testé sur deux Wemos, publié. **Fait** : 261029-0228 (file de commandes, redémarrages différés, une image toutes les 10 ms ; la structure EEPROM avec CRC passe à l'étape 4).
4. Mode autonome, passerelle, application web. **Fait** : 261029-1255, testé par OTA sur six barres (passation, secours, retour de la désignée, copie des scènes, Wi-Fi partagé, PoulpyNode voit toujours les barres sans MAC fantôme). Écart au plan : le Wi-Fi `PoulpyTube` n'existe que si la maison n'est pas joignable ; la passerelle ne change jamais de canal d'elle-même.
5. midiPoulpy adapté, retrait des anciens modes.
6. Ajustements Pollux / PoulpyNode (rôle passerelle sur la page Lampes, adresse 0).
