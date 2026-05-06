CC ?= gcc
PREFIX ?= $(HOME)/.local

NVML_INCLUDE ?= /usr/local/cuda/include
NVML_LIBDIR ?= /usr/lib/wsl/lib

CPPFLAGS ?= -isystem $(NVML_INCLUDE)
CFLAGS ?= -O3 -march=native -DNDEBUG -Wall -Wextra -Wno-deprecated-declarations
LDFLAGS ?= -L$(NVML_LIBDIR) -Wl,-rpath,$(NVML_LIBDIR)
LDLIBS ?= -lnvidia-ml

TARGETS := nvclean nvmini

.PHONY: all clean install uninstall

all: $(TARGETS)

nvclean: nvclean.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

nvmini: nvmini.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

install: $(TARGETS)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 nvclean $(DESTDIR)$(PREFIX)/bin/nvclean
	install -m 755 nvmini $(DESTDIR)$(PREFIX)/bin/nvmini

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/nvclean
	rm -f $(DESTDIR)$(PREFIX)/bin/nvmini

clean:
	rm -f $(TARGETS) *.o
