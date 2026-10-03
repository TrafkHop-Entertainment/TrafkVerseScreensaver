CC      ?= gcc
CFLAGS  ?= -O2 -Wall
LDLIBS  ?= -lSDL2 -lm

all: TrafkVerseScreensaver Icon.png

TrafkVerseScreensaver: TrafkVerseScreensaver.c tsf.h stb_image.h
	$(CC) $(CFLAGS) -o $@ TrafkVerseScreensaver.c $(LDLIBS)

tsf.h:
	@echo "Downloading TinySoundFont header..."
	wget -q -O $@ https://raw.githubusercontent.com/schellingb/TinySoundFont/master/tsf.h

stb_image.h:
	@echo "Downloading stb_image header (used to load the window/taskbar icon PNG)..."
	wget -q -O $@ https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

# Not a build dependency of the binary itself (compiling must still succeed
# even if this download fails) — just placed next to it so a plain 'make' +
# './TrafkVerseScreensaver' also shows an icon during local test runs.
# Install.sh fetches its own copy into the real install directory.
Icon.png:
	@echo "Downloading Icon.png (for local test runs)..."
	wget -q -O $@ https://raw.githubusercontent.com/TrafkHop-Entertainment/TrafkVerseScreensaver/main/Icon.png

run: TrafkVerseScreensaver
	./TrafkVerseScreensaver

menu: TrafkVerseScreensaver
	./TrafkVerseScreensaver --menu

clean:
	rm -f TrafkVerseScreensaver

distclean: clean
	rm -f tsf.h stb_image.h Icon.png

.PHONY: all run menu clean distclean
