"""Envoie l'API de sauvegarde PoulpyTube sur gaetanstreel.com.

Identifiants FTP : le .env de PoulpyLights (FTP_HOST, FTP_USER, FTP_PASS),
ou un autre fichier via POULPY_ENV. Rien de secret dans ce dépôt.

URL publique : https://poulpylights.gaetanstreel.com/poulpytube/api/backup.php
"""
import ftplib
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
ENV = Path(os.getenv("POULPY_ENV", HERE.parents[1] / "poulpylights_v3.0" / ".env"))
REMOTE = "/customers/3/3/2/gaetanstreel.com/httpd.www/poulpylights/poulpytube"
FILES = ["api/backup.php", "data/.htaccess"]


def load_env(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip().strip('"').strip("'"))


def cd_make(ftp, path):
    ftp.cwd("/")
    for part in [p for p in path.split("/") if p]:
        try:
            ftp.cwd(part)
        except ftplib.error_perm:
            ftp.mkd(part)
            ftp.cwd(part)


def main():
    load_env(ENV)
    with ftplib.FTP(os.environ["FTP_HOST"], timeout=30) as ftp:
        ftp.login(os.environ["FTP_USER"], os.environ["FTP_PASS"])
        for rel in FILES:
            remote_dir = f"{REMOTE}/{Path(rel).parent.as_posix()}"
            cd_make(ftp, remote_dir)
            with open(HERE / rel, "rb") as f:
                ftp.storbinary(f"STOR {Path(rel).name}", f)
            print("envoyé", rel)


if __name__ == "__main__":
    main()
