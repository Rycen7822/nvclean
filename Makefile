CC ?= gcc
PREFIX ?= $(HOME)/.local

NVML_INCLUDE ?= /usr/local/cuda/include
NVML_LIBDIR ?= /usr/lib/wsl/lib

CPPFLAGS ?= -isystem $(NVML_INCLUDE)
CFLAGS ?= -O3 -march=native -DNDEBUG -Wall -Wextra -Wno-deprecated-declarations
LDFLAGS ?= -L$(NVML_LIBDIR) -Wl,-rpath,$(NVML_LIBDIR)
LDLIBS ?= -lnvidia-ml

TARGET := nvclean
SRC := nvclean.c

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET) *.o
