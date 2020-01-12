# See mt76.h for possible channels
BUILD := DEBUG
CHANNEL := 1
VERSION := $(shell git describe --tags)

FLAGS := -Wall -Wpedantic -std=c++11 -MMD
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
DEFINES := -DCHANNEL=$(CHANNEL) -DVERSION=\"$(VERSION)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(DEFINES)
LDLIBS += -lstdc++ -lm -lusb-1.0 -lpthread
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(patsubst %.cpp,%.o,$(SOURCES)) firmware.o
DEPENDENCIES := $(OBJECTS:.o=.d)

PREFIX := /usr/local
BINDIR := $(PREFIX)/bin
UDEVDIR := /lib/udev/rules.d
MODPDIR := /lib/modprobe.d
SYSDDIR := /lib/systemd/system

.PHONY: all
all: xow

xow: $(OBJECTS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

firmware.o: firmware.bin
	$(LD) -r -b binary -o $@ $<

xow.service: xow.service.in
	sed 's|#BINDIR#|$(BINDIR)|' xow.service.in > xow.service

.PHONY: install
install: xow xow.service
	install -D -m 755 xow $(DESTDIR)$(BINDIR)/xow
	install -D -m 644 xow-udev.rules $(DESTDIR)$(UDEVDIR)/99-xow.rules
	install -D -m 644 xow-modprobe.conf $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	install -D -m 644 xow.service $(DESTDIR)$(SYSDDIR)/xow.service
	$(RM) xow.service

.PHONY: clean
clean:
	$(RM) xow $(OBJECTS) $(DEPENDENCIES)

-include $(DEPENDENCIES)
