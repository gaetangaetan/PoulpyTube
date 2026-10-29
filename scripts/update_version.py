#!/usr/bin/env python3
"""
Identifiant de build du récepteur, format YYMMDD-HHMM (identique aux releases FTP).

La version ne change que pour une release : POULPY_RELEASE=1 pio run.
Un build ordinaire garde build_version.h et data/version.txt tels quels
(sinon chaque compilation de test fabrique une « nouvelle version »).
"""

import datetime
import os
from pathlib import Path

try:
    PROJECT_DIR = Path(__file__).parent.parent
except NameError:
    PROJECT_DIR = Path(os.getcwd())

VERSION_INT_FILE = PROJECT_DIR / 'scripts' / '.version_int'
header = PROJECT_DIR / 'src' / 'build_version.h'
version_file = PROJECT_DIR / 'data' / 'version.txt'

release = os.environ.get('POULPY_RELEASE') == '1'

if not release and header.exists():
    print("Version recepteur inchangee (POULPY_RELEASE=1 pour une release)")
else:
    tag = datetime.datetime.now().strftime('%y%m%d-%H%M')

    # VERSION_INT : champ legacy 1 octet ESP-NOW.
    version_int = 1
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

    version_file.parent.mkdir(parents=True, exist_ok=True)
    version_file.write_text(tag)

    header.write_text(
        "// Genere automatiquement par scripts/update_version.py\n"
        "// Ne pas modifier manuellement !\n"
        "#ifndef BUILD_VERSION_H\n"
        "#define BUILD_VERSION_H\n\n"
        f'#define VERSION "{tag}"\n'
        f'#define VERSION_INT {version_int}\n\n'
        "#endif // BUILD_VERSION_H\n"
    )

    print(f"Version recepteur: {tag} (int={version_int})")
