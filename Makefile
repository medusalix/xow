BUILD := DEBUG
VERSION := $(shell git describe --tags)

FLAGS := -Wall -Wpedantic -std=c++11 -MMD -MP
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
DEFINES := -DVERSION=\"$(VERSION)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(DEFINES)
LDLIBS += -lpthread -lusb-1.0
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(SOURCES:.cpp=.o) firmware.o
DEPENDENCIES := $(SOURCES:.cpp=.d)

DRIVER_URL := http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2018/09/e5339a2a-0cbf-4100-ae09-81dab77d8ab2_56ceef39e5f673aa1ea7dd3c17e71b5bd2add2f7.cab
FIRMWARE_HASH := cef0b2a1a94c5a6407f1d198354d712eddc86dbc61a87223f4e4C53fe10ac92e

PREFIX := /usr/local
BINDIR := $(PREFIX)/bin
UDEVDIR := /etc/udev/rules.d
MODLDIR := /etc/modules-load.d
MODPDIR := /etc/modprobe.d
SYSDDIR := /etc/systemd/system

.PHONY: all
all: xow

xow: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

firmware.o: firmware.bin
	$(LD) -r -b binary -z noexecstack -o $@ $<

firmware.bin:
	curl -o driver.cab $(DRIVER_URL)
	cabextract -F FW_ACC_BR.bin driver.cab
	echo $(FIRMWARE_HASH) FW_ACC_BR.bin | sha256sum -c
	mv FW_ACC_BR.bin firmware.bin
	$(RM) driver.cab

.PHONY: install
install: xow
	sed 's|#BINDIR#|$(BINDIR)|' install/service.in > xow.service
	install -D -m 755 xow $(DESTDIR)$(BINDIR)/xow
	install -D -m 644 install/udev.rules $(DESTDIR)$(UDEVDIR)/50-xow.rules
	install -D -m 644 install/modules.conf $(DESTDIR)$(MODLDIR)/xow-uinput.conf
	install -D -m 644 install/modprobe.conf $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	install -D -m 644 xow.service $(DESTDIR)$(SYSDDIR)/xow.service
	$(RM) xow.service

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/xow
	$(RM) $(DESTDIR)$(UDEVDIR)/50-xow.rules
	$(RM) $(DESTDIR)$(MODLDIR)/xow-uinput.conf
	$(RM) $(DESTDIR)$(MODPDIR)/xow-blacklist.conf
	$(RM) $(DESTDIR)$(SYSDDIR)/xow.service

.PHONY: clean
clean:
	$(RM) xow $(OBJECTS) $(DEPENDENCIES)

-include $(DEPENDENCIES)
