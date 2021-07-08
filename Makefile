BUILD := DEBUG
VERSION := $(shell git describe --tags)
FIRMWARE := "/lib/firmware/xow_dongle.bin"

FLAGS := -Wall -Wpedantic -std=c++11 -MMD -MP
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
DEFINES := -DVERSION=\"$(VERSION)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(DEFINES)
LDLIBS += -lpthread -lusb-1.0
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(SOURCES:.cpp=.o) firmware.o
DEPENDENCIES := $(SOURCES:.cpp=.d)

DRIVER_URL := http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab
FIRMWARE_HASH := 48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66

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

firmware.o: firmware.bin
	$(LD) -r -b binary -z noexecstack -o $@ $<

firmware.bin:
	curl -o driver.cab $(DRIVER_URL)
	cabextract -F FW_ACC_00U.bin driver.cab
	echo $(FIRMWARE_HASH) FW_ACC_00U.bin | sha256sum -c
	mv FW_ACC_00U.bin firmware.bin
	$(RM) driver.cab

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
