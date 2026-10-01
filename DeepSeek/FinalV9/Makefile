CC      ?= gcc
CFLAGS  ?= -O2 -Wall
LDLIBS  ?= -lSDL2 -lm

all: noise

noise: noise.c tsf.h
	$(CC) $(CFLAGS) -o $@ noise.c $(LDLIBS)

tsf.h:
	@echo "Downloading TinySoundFont header..."
	wget -q -O $@ https://raw.githubusercontent.com/schellingb/TinySoundFont/master/tsf.h

run: noise
	./noise

clean:
	rm -f noise

distclean: clean
	rm -f tsf.h

.PHONY: all run clean distclean