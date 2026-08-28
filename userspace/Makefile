CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -I../include

.PHONY: all clean

all: pulseforge-cat pulseforge-ctl

pulseforge-cat: pulseforge-cat.c ../include/pulseforge_uapi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ pulseforge-cat.c

pulseforge-ctl: pulseforge-ctl.c ../include/pulseforge_uapi.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ pulseforge-ctl.c

clean:
	$(RM) pulseforge-cat pulseforge-ctl

