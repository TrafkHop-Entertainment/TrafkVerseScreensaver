#!/bin/sh
# Baut ./randomnoise als EINE Datei: SDL2 statisch eingebaut.
# Wayland, X11, PipeWire, PulseAudio, ALSA, EGL werden von SDL erst zur Laufzeit per dlopen
# geladen (falls vorhanden) -> keine Systeminstallation noetig, laeuft auch ohne diese Libs (dann ohne die Funktion).
# Benoetigt nur zum BAUEN: gcc, make, curl und die -dev Pakete (libwayland-dev libxkbcommon-dev libpipewire-0.3-dev libpulse-dev libasound2-dev libx11-dev ...).
set -e
SDLV=2.30.12
cd "$(dirname "$0")"
if [ ! -f sdl-static/lib/libSDL2.a ]; then
  [ -f SDL2-$SDLV.tar.gz ] || curl -L -o SDL2-$SDLV.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDLV/SDL2-$SDLV.tar.gz
  tar xf SDL2-$SDLV.tar.gz
  ( cd SDL2-$SDLV && ./configure --prefix="$PWD/../sdl-static" --disable-shared --enable-static \
      --enable-video-wayland --enable-video-x11 --enable-pipewire --enable-pulseaudio --enable-alsa \
      --disable-video-kmsdrm --disable-jack --disable-esd --disable-sndio --disable-arts --disable-nas \
      --disable-joystick --disable-haptic --disable-sensor --disable-hidapi --disable-power --disable-libudev --disable-dbus --disable-ime \
      --enable-video-wayland-qt-touch=no && make -j"$(nproc)" && make install )
fi
gcc -O2 -o randomnoise noise.c $(sdl-static/bin/sdl2-config --cflags) $(sdl-static/bin/sdl2-config --static-libs | sed 's/-Wl,--no-undefined//') -lm -ldl -lpthread
strip randomnoise
echo "fertig: $(ls -la randomnoise)"; ldd randomnoise
