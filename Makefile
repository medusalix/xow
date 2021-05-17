BUILD := DEBUG
VERSION := $(shell git describe --tags)
FIRMWARE := "/lib/firmware/xow_dongle.bin"

FLAGS := -Wall -Wpedantic -std=c++11 -MMD -MP
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
DEFINES := -DVERSION=\"$(VERSION)\" -DFIRMWARE=\"$(FIRMWARE)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(DEFINES)
LDLIBS += -lpthread -lusb-1.0
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(SOURCES:.cpp=.o)
DEPENDENCIES := $(SOURCES:.cpp=.d)

PREFIX := /usr/local
BINDIR := $(PREFIX)/bin
UDEVDIR := /etc/udev/rules.d
MODLDIR := /etc/modules-load.d
MODPDIR := /etc/modprobe.d
SYSDDIR := /etc/systemd/system

.PHONY: all
all: xow xow-get-firmware.sh xow.service

xow: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<


xow-get-firmware.sh: install/xow-get-firmware.sh.in
	sed "s|#FIRMWARE#|$(FIRMWARE)|" $< > $@

xow.service: install/service.in
	sed 's|#BINDIR#|$(BINDIR)|' $< > $@

.PHONY: install
install: all
	install -D -m 755 xow $(DESTDIR)$(BINDIR)/xow
	install -D -m 755 xow-get-firmware.sh $(DESTDIR)$(BINDIR)/xow-get-firmware.sh
	install -D -m 644 install/udev.rules $(DESTDIR)$(UDEVDIR)/50-xow.rules
	install -D -m 644 install/modules.conf $(DESTDIR)$(MODLDIR)/xow-uinput.conf
	install -D -m 644 install/modprobe.conf $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	install -D -m 644 xow.service $(DESTDIR)$(SYSDDIR)/xow.service

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/xow
	$(RM) $(DESTDIR)$(BINDIR)/xow-get-firmware.sh
	$(RM) $(DESTDIR)$(UDEVDIR)/50-xow.rules
	$(RM) $(DESTDIR)$(MODLDIR)/xow-uinput.conf
	$(RM) $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	$(RM) $(DESTDIR)$(SYSDDIR)/xow.service

.PHONY: clean
clean:
	$(RM) xow $(OBJECTS) $(DEPENDENCIES)
	$(RM) xow-get-firmware.sh
	$(RM) xow.service

-include $(DEPENDENCIES)
