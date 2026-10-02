CC      ?= gcc
CFLAGS  ?= -O2 -Wall
LDLIBS  ?= -lSDL2 -lm

all: TrafkVerseScreensaver

TrafkVerseScreensaver: TrafkVerseScreensaver.c tsf.h
	$(CC) $(CFLAGS) -o $@ TrafkVerseScreensaver.c $(LDLIBS)

tsf.h:
	@echo "Downloading TinySoundFont header..."
	wget -q -O $@ https://raw.githubusercontent.com/schellingb/TinySoundFont/master/tsf.h

run: TrafkVerseScreensaver
	./TrafkVerseScreensaver

menu: TrafkVerseScreensaver
	./TrafkVerseScreensaver --menu

clean:
	rm -f TrafkVerseScreensaver

distclean: clean
	rm -f tsf.h

.PHONY: all run menu clean distclean
