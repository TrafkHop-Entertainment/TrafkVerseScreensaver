#!/usr/bin/env bash
# Install.sh – TrafkVerseScreensaver (DeepSeek/FinalV9) herunterladen, bauen und installieren
set -euo pipefail

# ---------------------------------------------------------------- Konfiguration
# Welcher Stand wird geholt? "HEAD" = immer die neueste Version des Standard-Branches.
# Zum Festpinnen auf einen Stand statt HEAD einen Commit-Hash eintragen
# (oder beim Aufruf: SCREENSAVER_REF=<hash> ./Install.sh)
SCREENSAVER_REF="${SCREENSAVER_REF:-HEAD}"
SF2_REF="${SF2_REF:-HEAD}"
ICON_REF="${ICON_REF:-HEAD}"

BASE_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/TrafkVerseScreensaver/${SCREENSAVER_REF}/DeepSeek/FinalV9"
SF2_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/SourceHop-Audios/${SF2_REF}/TrafkSF2.sf2"

INSTALL_DIR="${HOME}/.local/share/TrafkHopEntertainment/TrafkVerseScreensaver"
ICON_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/TrafkVerseScreensaver/${ICON_REF}/Icon.png"

DESKTOP_DIR="${HOME}/.local/share/applications"
DESKTOP_FILE="${DESKTOP_DIR}/TrafkVerseScreensaver.desktop"

# ---------------------------------------------------------------- Hilfsfunktionen
info() { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[✓]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[!]\033[0m %s\n' "$*" >&2; }

need() {
    command -v "$1" >/dev/null 2>&1 || { err "'$1' fehlt. Bitte installieren (siehe Hinweis unten)."; MISSING=1; }
}

# ---------------------------------------------------------------- Abhängigkeiten prüfen
MISSING=0
need wget
need make
need gcc
if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists sdl2 || { err "SDL2-Entwicklungsdateien fehlen."; MISSING=1; }
else
    err "pkg-config fehlt (wird zur Prüfung von SDL2 benötigt)."; MISSING=1
fi

if [ "$MISSING" -ne 0 ]; then
    cat >&2 <<'EOF'

Benötigte Pakete installieren, z. B.:
  Debian/Ubuntu/Mint : sudo apt install build-essential wget pkg-config libsdl2-dev
  Fedora             : sudo dnf install gcc make wget pkgconf-pkg-config SDL2-devel
  Arch/Manjaro       : sudo pacman -S base-devel wget sdl2
EOF
    exit 1
fi

# ---------------------------------------------------------------- Build in temporärem Ordner
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

info "Lade noise.c und Makefile herunter ..."
wget -q --show-progress -O "${BUILD_DIR}/noise.c"  "${BASE_URL}/noise.c"
wget -q --show-progress -O "${BUILD_DIR}/Makefile" "${BASE_URL}/Makefile"

info "Baue Screensaver (das Makefile lädt tsf.h automatisch) ..."
make -C "$BUILD_DIR"
[ -x "${BUILD_DIR}/noise" ] || { err "Build fehlgeschlagen: 'noise' wurde nicht erzeugt."; exit 1; }
ok "Build erfolgreich."

# ---------------------------------------------------------------- Installation
# Ordner prüfen (Symlinks werden wie üblich verfolgt)
PARENT_DIR="$(dirname "$INSTALL_DIR")"   # .../TrafkHopEntertainment

if [ -L "$PARENT_DIR" ] && [ ! -d "$PARENT_DIR" ]; then
    err "${PARENT_DIR} ist ein kaputter Symlink (Ziel: $(readlink "$PARENT_DIR"))."
    exit 1
fi

if [ -d "$PARENT_DIR" ]; then
    if [ -L "$PARENT_DIR" ]; then
        info "TrafkHopEntertainment existiert (Symlink -> $(readlink -f "$PARENT_DIR")), nutze ihn."
    else
        info "TrafkHopEntertainment existiert bereits, nutze ihn."
    fi
fi

if [ -d "$INSTALL_DIR" ]; then
    info "Zielordner existiert bereits: ${INSTALL_DIR}"
else
    info "Erstelle Zielordner: ${INSTALL_DIR}"
    mkdir -p "$INSTALL_DIR"   # legt fehlende Ordner an, folgt bestehenden Symlinks
fi

# SoundFont zuerst in den Build-Ordner laden, damit eine bestehende Datei
# bei einem fehlgeschlagenen Download nicht kaputtgeht
info "Lade Soundfont (TrafkSF2.sf2) herunter ..."
wget -q --show-progress -O "${BUILD_DIR}/TrafkSF2.sf2" "$SF2_URL"

# Plausibilitätscheck: eine echte SF2-Datei beginnt mit "RIFF"
if [ "$(head -c 4 "${BUILD_DIR}/TrafkSF2.sf2")" != "RIFF" ]; then
    err "TrafkSF2.sf2 sieht nicht wie eine gültige SoundFont aus (evtl. Git-LFS-Platzhalter?)."
    err "Der Screensaver läuft dann ohne Ton. Prüfe die URL: ${SF2_URL}"
fi

info "Lade Icon (Icon.png) herunter ..."
wget -q --show-progress -O "${BUILD_DIR}/Icon.png" "$ICON_URL"
if [ "$(head -c 4 "${BUILD_DIR}/Icon.png" | tail -c 3)" != "PNG" ]; then
    err "Icon.png ist keine gültige PNG-Datei. Prüfe die URL: ${ICON_URL}"
    exit 1
fi

# Läuft noch eine alte Instanz? (wird nicht beendet, nur gemeldet)
if pgrep -x noise >/dev/null 2>&1; then
    info "Hinweis: 'noise' läuft gerade. Die neue Version wird erst nach einem Neustart des Screensavers genutzt."
fi

# Dateien kopieren; bestehende werden überschrieben
for f in noise TrafkSF2.sf2 Icon.png; do
    if [ -e "${INSTALL_DIR}/${f}" ] || [ -L "${INSTALL_DIR}/${f}" ]; then
        info "${f} existiert bereits, wird überschrieben."
    else
        info "Installiere ${f} ..."
    fi
done

# noise: erst unter temporärem Namen im Zielordner ablegen, dann per mv ersetzen.
# mv ist atomar und klappt auch, wenn die alte Datei gerade läuft ("Text file busy").
install -m 755 "${BUILD_DIR}/noise" "${INSTALL_DIR}/.noise.new"
mv -f "${INSTALL_DIR}/.noise.new" "${INSTALL_DIR}/noise"

install -m 644 "${BUILD_DIR}/TrafkSF2.sf2" "${INSTALL_DIR}/TrafkSF2.sf2"
install -m 644 "${BUILD_DIR}/Icon.png"     "${INSTALL_DIR}/Icon.png"

# Kontrolle: stimmen die installierten Dateien mit den frisch gebauten/geladenen überein?
for f in noise TrafkSF2.sf2 Icon.png; do
    if ! cmp -s "${BUILD_DIR}/${f}" "${INSTALL_DIR}/${f}"; then
        err "${f} im Zielordner stimmt nicht mit der neuen Version überein!"
        exit 1
    fi
done
ok "Alle Dateien installiert und verifiziert."

# ---------------------------------------------------------------- .desktop-Datei
[ -e "$DESKTOP_FILE" ] && info ".desktop-Datei existiert bereits, wird überschrieben." || info "Erstelle .desktop-Datei ..."
mkdir -p "$DESKTOP_DIR"
cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=TrafkVerseScreensaver
Exec=${INSTALL_DIR}/noise
Path=${INSTALL_DIR}
Icon=${INSTALL_DIR}/Icon.png
Terminal=false
Categories=Game;
EOF
chmod 644 "$DESKTOP_FILE"

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

# ---------------------------------------------------------------- Fertig
ok "Installation abgeschlossen!"
echo "    Programm : ${INSTALL_DIR}/noise"
echo "    SoundFont: ${INSTALL_DIR}/TrafkSF2.sf2"
echo "    Icon     : ${INSTALL_DIR}/Icon.png"
echo "    Starter  : ${DESKTOP_FILE}"