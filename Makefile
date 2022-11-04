CC      ?= gcc
CFLAGS  ?= -O0 -ggdb -pedantic -Wall -I /usr/include/libdrm
LDFLAGS ?= -ldrm

OBJ = main.o framebuffer.o
HDR = framebuffer.h globals.h
PROGNAME = drm-framebuffer

exec_prefix ?= /usr
bindir ?= $(exec_prefix)/bin

all: $(OBJ) $(HDR)
	$(CC) $(CFLAGS) -o $(PROGNAME) $(OBJ) $(LDFLAGS)

install: all
	install -d $(DESTDIR)$(bindir)
	install -m 0755 $(PROGNAME) $(DESTDIR)$(bindir)

clean:
	@echo "Clean object files"
	@rm -f $(OBJ)
	@rm -f $(PROGNAME)

%.o: %.c
	$(CC) $(CFLAGS) -c $<
