#!/usr/bin/env python3
"""
Met à jour l'identifiant de build du récepteur avant chaque compilation.
Format : YYMMDD-HHMM (identique à l'émetteur et aux releases FTP).
"""

import datetime
import os
from pathlib import Path

try:
    PROJECT_DIR = Path(__file__).parent.parent
except NameError:
    PROJECT_DIR = Path(os.getcwd())

VERSION_INT_FILE = PROJECT_DIR / 'scripts' / '.version_int'
tag = datetime.datetime.now().strftime('%y%m%d-%H%M')

# Incrémenter VERSION_INT (champ legacy 1-byte ESP-NOW).
# Sans compteur local, repartir du #define déjà écrit dans build_version.h.
version_int = 1
header = PROJECT_DIR / 'src' / 'build_version.h'
if VERSION_INT_FILE.exists():
    try:
        version_int = int(VERSION_INT_FILE.read_text().strip()) + 1
    except ValueError:
        version_int = 1
elif header.exists():
    for line in header.read_text(encoding='utf-8').splitlines():
        if line.startswith('#define VERSION_INT '):
            try:
                version_int = int(line.split()[-1]) + 1
            except ValueError:
                version_int = 1
            break
VERSION_INT_FILE.write_text(str(version_int))

# data/version.txt — lu par publish_release.py
version_file = PROJECT_DIR / 'data' / 'version.txt'
version_file.parent.mkdir(parents=True, exist_ok=True)
version_file.write_text(tag)

# src/build_version.h — inclus par main.cpp
build_version_file = PROJECT_DIR / 'src' / 'build_version.h'
build_version_file.write_text(
    "// Genere automatiquement par scripts/update_version.py\n"
    "// Ne pas modifier manuellement !\n"
    "#ifndef BUILD_VERSION_H\n"
    "#define BUILD_VERSION_H\n\n"
    f'#define VERSION "{tag}"\n'
    f'#define VERSION_INT {version_int}\n\n'
    "#endif // BUILD_VERSION_H\n"
)

print(f"Version recepteur: {tag} (int={version_int})")
