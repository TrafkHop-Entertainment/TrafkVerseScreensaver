#!/usr/bin/env bash
# Install.sh – TrafkVerseScreensaver (NewestVersion, vorkompiliert) herunterladen und installieren
set -euo pipefail

# ---------------------------------------------------------------- Konfiguration
# Welcher Stand wird geholt? "HEAD" = immer die neueste Version des Standard-Branches.
# Zum Festpinnen auf einen Stand statt HEAD einen Commit-Hash eintragen
# (oder beim Aufruf: SCREENSAVER_REF=<hash> ./Install.sh)
# SCREENSAVER_REF steuert jetzt den Ref für die vorkompilierte Executable
# (nicht mehr für Quellcode+Makefile – das Bauen passiert nicht mehr hier).
SCREENSAVER_REF="${SCREENSAVER_REF:-HEAD}"
SF2_REF="${SF2_REF:-HEAD}"
ICON_REF="${ICON_REF:-HEAD}"
LICENSE_REF="${LICENSE_REF:-HEAD}"

BASE_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/TrafkVerseScreensaver/${SCREENSAVER_REF}/NewestVersion"
BINARY_URL="${BASE_URL}/TrafkVerseScreensaver"
SF2_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/SourceHop-Audios/${SF2_REF}/TrafkSF2.sf2"

INSTALL_DIR="${HOME}/.local/share/TrafkHopEntertainment/TrafkVerseScreensaver"
ICON_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/TrafkVerseScreensaver/${ICON_REF}/Icon.png"
LICENSE_URL="https://raw.githubusercontent.com/TrafkHop-Entertainment/TrafkVerseScreensaver/${LICENSE_REF}/LICENSE"

DESKTOP_DIR="${HOME}/.local/share/applications"
DESKTOP_FILE="${DESKTOP_DIR}/TrafkVerseScreensaver.desktop"

# Theme icon name used in the .desktop file (and for icon-theme lookups by
# docks/taskbars). Icon.png also gets installed under this name into the
# user's hicolor icon theme — see the ".desktop-Datei"/"Icon-Theme" section
# below for why an absolute path alone isn't enough for every consumer.
ICON_NAME="TrafkVerseScreensaver"
ICON_THEME_DIR="${HOME}/.local/share/icons/hicolor/256x256/apps"

# ---------------------------------------------------------------- Hilfsfunktionen
info() { printf '\033[1;34m[*]\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m[✓]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[!]\033[0m %s\n' "$*" >&2; }

need() {
    command -v "$1" >/dev/null 2>&1 || { err "'$1' fehlt. Bitte installieren (siehe Hinweis unten)."; MISSING=1; }
}

# ---------------------------------------------------------------- Abhängigkeiten prüfen
# Kein Compiler/Build-Werkzeug mehr nötig – es wird nur noch die fertige
# Executable heruntergeladen. gcc/make/pkg-config/SDL2-dev bleiben Sache des
# Makefiles, das nur noch für den eigenen, lokalen Build gedacht ist.
MISSING=0
need wget
if [ "$MISSING" -ne 0 ]; then
    cat >&2 <<'EOF'

Benötigte Pakete installieren, z. B.:
  Debian/Ubuntu/Mint : sudo apt install wget
  Fedora             : sudo dnf install wget
  Arch/Manjaro       : sudo pacman -S wget
EOF
    exit 1
fi

# ---------------------------------------------------------------- Download in temporärem Ordner
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

info "Lade TrafkVerseScreensaver (vorkompiliert) herunter ..."
wget -q --show-progress -O "${BUILD_DIR}/TrafkVerseScreensaver" "$BINARY_URL"

# Plausibilitätscheck: eine echte ELF-Executable beginnt mit dem Magic-Byte
# 0x7F 'E' 'L' 'F' – fängt z. B. ab, dass statt der Datei aus Versehen eine
# 404-HTML-Seite heruntergeladen wurde.
if [ "$(head -c 4 "${BUILD_DIR}/TrafkVerseScreensaver" | tail -c 3)" != "ELF" ]; then
    err "TrafkVerseScreensaver sieht nicht wie eine gültige ausführbare Datei aus (evtl. falsche URL oder Git-LFS-Platzhalter?)."
    err "Prüfe die URL: ${BINARY_URL}"
    exit 1
fi
chmod +x "${BUILD_DIR}/TrafkVerseScreensaver"
ok "Download erfolgreich."

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

info "Lade LICENSE herunter ..."
wget -q --show-progress -O "${BUILD_DIR}/LICENSE" "$LICENSE_URL"
# Plain text hat kein Magic-Byte wie PNG/RIFF; ein einfacher Nicht-leer-Check
# reicht hier, um zumindest einen 404-als-leere-Datei-Fall abzufangen.
if [ ! -s "${BUILD_DIR}/LICENSE" ]; then
    err "LICENSE ist leer. Prüfe die URL: ${LICENSE_URL}"
    exit 1
fi

# Läuft noch eine alte Instanz? (wird nicht beendet, nur gemeldet)
if pgrep -x TrafkVerseScreensaver >/dev/null 2>&1; then
    info "Hinweis: 'TrafkVerseScreensaver' läuft gerade. Die neue Version wird erst nach einem Neustart des Screensavers genutzt."
fi

# Dateien kopieren; bestehende werden überschrieben
for f in TrafkVerseScreensaver TrafkSF2.sf2 Icon.png LICENSE; do
    if [ -e "${INSTALL_DIR}/${f}" ] || [ -L "${INSTALL_DIR}/${f}" ]; then
        info "${f} existiert bereits, wird überschrieben."
    else
        info "Installiere ${f} ..."
    fi
done

# TrafkVerseScreensaver: erst unter temporärem Namen im Zielordner ablegen, dann per mv ersetzen.
# mv ist atomar und klappt auch, wenn die alte Datei gerade läuft ("Text file busy").
install -m 755 "${BUILD_DIR}/TrafkVerseScreensaver" "${INSTALL_DIR}/.TrafkVerseScreensaver.new"
mv -f "${INSTALL_DIR}/.TrafkVerseScreensaver.new" "${INSTALL_DIR}/TrafkVerseScreensaver"

install -m 644 "${BUILD_DIR}/TrafkSF2.sf2" "${INSTALL_DIR}/TrafkSF2.sf2"
install -m 644 "${BUILD_DIR}/Icon.png"     "${INSTALL_DIR}/Icon.png"
install -m 644 "${BUILD_DIR}/LICENSE"      "${INSTALL_DIR}/LICENSE"

# Kontrolle: stimmen die installierten Dateien mit den frisch geladenen überein?
for f in TrafkVerseScreensaver TrafkSF2.sf2 Icon.png LICENSE; do
    if ! cmp -s "${BUILD_DIR}/${f}" "${INSTALL_DIR}/${f}"; then
        err "${f} im Zielordner stimmt nicht mit der neuen Version überein!"
        exit 1
    fi
done
ok "Alle Dateien installiert und verifiziert."

# ---------------------------------------------------------------- Icon-Theme
# Programme, die Icons ueber ein Icon-Theme aufloesen (gtk_icon_theme_has_icon
# & Co. - Taskbars, Docks, App-Grids), suchen nach einem NAMEN wie
# "TrafkVerseScreensaver", nicht nach dem obigen absoluten Pfad in
# INSTALL_DIR. "hicolor" ist das universelle Fallback-Theme, das jede
# konforme Implementierung immer mitdurchsucht, egal welches Theme sonst
# aktiv ist - daher landet die Datei dort, unter dem Namen, nicht der
# ICON_NAME-Variable als Dateiname mit Endung.
info "Installiere Icon ins Icon-Theme (hicolor) ..."
mkdir -p "$ICON_THEME_DIR"
install -m 644 "${BUILD_DIR}/Icon.png" "${ICON_THEME_DIR}/${ICON_NAME}.png"
command -v gtk-update-icon-cache >/dev/null 2>&1 && \
    gtk-update-icon-cache -q -t -f "${HOME}/.local/share/icons/hicolor" 2>/dev/null || true

# ---------------------------------------------------------------- .desktop-Datei
DESKTOP_ENTRY_CONTENT="[Desktop Entry]
Type=Application
Name=TrafkVerseScreensaver
Exec=${INSTALL_DIR}/TrafkVerseScreensaver
Path=${INSTALL_DIR}
Icon=${ICON_NAME}
StartupWMClass=${ICON_NAME}
Terminal=false
Categories=Game;"

[ -e "$DESKTOP_FILE" ] && info ".desktop-Datei existiert bereits, wird überschrieben." || info "Erstelle .desktop-Datei ..."
mkdir -p "$DESKTOP_DIR"
printf '%s\n' "$DESKTOP_ENTRY_CONTENT" > "$DESKTOP_FILE"
chmod 644 "$DESKTOP_FILE"

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

# ---------------------------------------------------------------- Desktop-Verknüpfung
# Nur wenn der Nutzer ueberhaupt einen Desktop-Ordner hat. xdg-user-dir ist
# der korrekte, lokalisierungssichere Weg (der Ordner heisst nicht ueberall
# "Desktop", z.B. auf Deutsch "Schreibtisch"); HOME/Desktop ist der Fallback,
# falls xdg-user-dir fehlt.
if command -v xdg-user-dir >/dev/null 2>&1; then
    USER_DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
else
    USER_DESKTOP_DIR="${HOME}/Desktop"
fi

if [ -n "$USER_DESKTOP_DIR" ] && [ -d "$USER_DESKTOP_DIR" ]; then
    DESKTOP_SHORTCUT="${USER_DESKTOP_DIR}/TrafkVerseScreensaver.desktop"
    info "Erstelle Desktop-Verknüpfung (${DESKTOP_SHORTCUT}) ..."
    printf '%s\n' "$DESKTOP_ENTRY_CONTENT" > "$DESKTOP_SHORTCUT"
    # Ausfuehrbar machen: auf den meisten Desktops (inkl. Nautilus) sonst
    # nur eine inerte Textdatei statt eines klickbaren Launchers.
    chmod 755 "$DESKTOP_SHORTCUT"
    # Nautilus zeigt ohne diesen Schritt nur ein Platzhalter-Icon mit
    # Warndreieck an ("nicht vertrauenswuerdig") statt Icon.png; entspricht
    # dem Klick auf "Starten erlauben" im Dateimanager. Auf Desktops ohne
    # Nautilus (KDE, XFCE, ...) ist das ein wirkungsloser No-Op.
    command -v gio >/dev/null 2>&1 && \
        gio set "$DESKTOP_SHORTCUT" "metadata::trusted" yes 2>/dev/null || true
    ok "Desktop-Verknüpfung erstellt."
else
    info "Kein Desktop-Ordner gefunden, überspringe Desktop-Verknüpfung."
fi

# ---------------------------------------------------------------- Fertig
ok "Installation abgeschlossen!"
echo "    Programm : ${INSTALL_DIR}/TrafkVerseScreensaver"
echo "    SoundFont: ${INSTALL_DIR}/TrafkSF2.sf2"
echo "    Icon     : ${INSTALL_DIR}/Icon.png"
echo "    LICENSE  : ${INSTALL_DIR}/LICENSE"
echo "    Icon-Theme: ${ICON_THEME_DIR}/${ICON_NAME}.png"
echo "    Starter  : ${DESKTOP_FILE}"
[ -n "${DESKTOP_SHORTCUT:-}" ] && [ -e "${DESKTOP_SHORTCUT:-}" ] && echo "    Desktop  : ${DESKTOP_SHORTCUT}"