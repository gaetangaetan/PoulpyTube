#!/usr/bin/env python3
"""
Compresse web/index.html dans src/web_page.h, servi par la passerelle.
Le fichier n'est réécrit que si la page a changé (pas de recompilation inutile).
"""

import gzip
import os
from pathlib import Path

try:
    PROJECT_DIR = Path(__file__).parent.parent
except NameError:
    PROJECT_DIR = Path(os.getcwd())

src = PROJECT_DIR / 'web' / 'index.html'
dst = PROJECT_DIR / 'src' / 'web_page.h'

data = gzip.compress(src.read_bytes(), compresslevel=9, mtime=0)
lines = []
for i in range(0, len(data), 24):
    lines.append('  ' + ', '.join(f'0x{b:02X}' for b in data[i:i + 24]) + ',')

text = (
    "// Genere par scripts/embed_web.py depuis web/index.html. Ne pas modifier.\n"
    "#pragma once\n"
    "#include <Arduino.h>\n\n"
    f"static const size_t WEB_PAGE_GZ_LEN = {len(data)};\n"
    "static const uint8_t WEB_PAGE_GZ[] PROGMEM = {\n"
    + "\n".join(lines)
    + "\n};\n"
)

if not dst.exists() or dst.read_text(encoding='utf-8') != text:
    dst.write_text(text, encoding='utf-8')
print(f"Page web: {src.stat().st_size} -> {len(data)} octets")
