BUILD := DEBUG
VERSION := $(shell git describe --tags)

FLAGS := -Wall -Wpedantic -std=c++11 -MMD -MP -Iexternal/headers
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
DEFINES := -DVERSION=\"$(VERSION)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(DEFINES)
LDLIBS += -lpthread -ludev
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(SOURCES:.cpp=.o) firmware.o libusb-1.0.a
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
all: xow

xow: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

firmware.o: firmware.bin
	$(LD) -r -b binary -z noexecstack -o $@ $<

libusb-1.0.a:
	cd external/libusb && ./autogen.sh
	make -C external/libusb
	cp external/libusb/libusb/.libs/libusb-1.0.a .

firmware.bin:
	curl -o driver.cab $(DRIVER_URL)
	cabextract -F FW_ACC_00U.bin driver.cab
	echo $(FIRMWARE_HASH) FW_ACC_00U.bin | sha256sum -c
	mv FW_ACC_00U.bin firmware.bin
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
