#!/bin/sh
# ./build.sh          -> nutzt das auf dem System installierte SDL2 (klein, schnell)
# ./build.sh static   -> baut SDL2 selbst und linkt es ein: EINE Datei, laeuft auch ohne SDL2 auf dem Zielsystem
#                        (Wayland/PipeWire/Pulse/ALSA/X11 werden erst zur Laufzeit per dlopen geladen)
set -e
cd "$(dirname "$0")"
if [ "$1" != "static" ]; then
  gcc -O2 -o randomnoise noise.c $(pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags) \
      $(pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs) -lm
else
  SDLV=2.32.10; PFX="$(pwd)/sdl-static"
  if [ ! -f sdl-static/lib/libSDL2.a ]; then
    [ -f SDL2-$SDLV.tar.gz ] || curl -L -o SDL2-$SDLV.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$SDLV/SDL2-$SDLV.tar.gz
    tar xf SDL2-$SDLV.tar.gz
    ( cd SDL2-$SDLV && ./configure --prefix="$PFX" --disable-shared --enable-static \
        --enable-video-wayland --enable-video-x11 --enable-pipewire --enable-pulseaudio --enable-alsa \
        --disable-video-kmsdrm --disable-jack --disable-sndio --disable-joystick --disable-haptic --disable-sensor \
        --disable-hidapi --disable-power --disable-libudev --disable-dbus --disable-ime && make -j"$(nproc)" && make install )
  fi
  gcc -O2 -o randomnoise noise.c $(sdl-static/bin/sdl2-config --cflags) \
      $(sdl-static/bin/sdl2-config --static-libs | sed 's/-Wl,--no-undefined//') -lm -ldl -lpthread
  strip randomnoise
fi
echo "fertig:"; ls -la randomnoise
