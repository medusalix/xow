BUILD := DEBUG
VERSION := $(shell git describe --tags)
LOCK_FILE := /tmp/xow.lock

FLAGS := -Wall -Wpedantic -std=c++11 -MMD
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
DEFINES := -DVERSION=\"$(VERSION)\" -DLOCK_FILE=\"$(LOCK_FILE)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(DEFINES)
LDLIBS += -lstdc++ -lm -lpthread -lusb-1.0
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(patsubst %.cpp,%.o,$(SOURCES)) firmware.o
DEPENDENCIES := $(OBJECTS:.o=.d)

PREFIX := /usr/local
BINDIR := $(PREFIX)/bin
UDEVDIR := /lib/udev/rules.d
MODLDIR := /lib/modules-load.d
MODPDIR := /lib/modprobe.d
SYSDDIR := /lib/systemd/system

.PHONY: all
all: xow

xow: $(OBJECTS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

firmware.o: firmware.bin
	$(LD) -r -b binary -o $@ $<

xow.service: install/service.in
	sed 's|#BINDIR#|$(BINDIR)|' $< > $@

.PHONY: install
install: xow xow.service
	install -D -m 755 xow $(DESTDIR)$(BINDIR)/xow
	install -D -m 644 install/udev.rules $(DESTDIR)$(UDEVDIR)/99-xow.rules
	install -D -m 644 install/modules.conf $(DESTDIR)$(MODLDIR)/xow-uinput.conf
	install -D -m 644 install/modprobe.conf $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	install -D -m 644 xow.service $(DESTDIR)$(SYSDDIR)/xow.service
	$(RM) xow.service

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/xow
	$(RM) $(DESTDIR)$(UDEVDIR)/99-xow.rules
	$(RM) $(DESTDIR)$(MODLDIR)/xow-uinput.conf
	$(RM) $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	$(RM) $(DESTDIR)$(SYSDDIR)/xow.service

.PHONY: clean
clean:
	$(RM) xow $(OBJECTS) $(DEPENDENCIES)

-include $(DEPENDENCIES)
