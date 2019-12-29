# See mt76.h for possible channels
BUILD := DEBUG
CHANNEL := 1
VERSION := $(shell git describe --tags)

FLAGS := -Wall -Wpedantic -std=c++11 -MMD
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
MACROS := -DCHANNEL=$(CHANNEL) -DVERSION=\"$(VERSION)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(MACROS)
LDLIBS += -lstdc++ -lm -lusb-1.0 -lpthread
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(patsubst %.cpp,%.o,$(SOURCES)) firmware.o
DEPENDENCIES := $(OBJECTS:.o=.d)

DRIVER_URL := http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab
FIRMWARE_HASH := 48084d9fa53b9bb04358f3bb127b7495dc8f7bb0b3ca1437bd24ef2b6eabdf66

INSTALL_PATH := /usr/local/bin
SERVICE_PATH := /etc/systemd/user

xow: $(OBJECTS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

firmware.bin:
	curl -o driver.cab $(DRIVER_URL)
	cabextract -F FW_ACC_00U.bin driver.cab
	echo $(FIRMWARE_HASH) FW_ACC_00U.bin | sha256sum -c
	mv FW_ACC_00U.bin firmware.bin
	$(RM) driver.cab

firmware.o: firmware.bin
	ld -r -b binary -o $@ $<

.PHONY: install
install: xow
	cp xow $(INSTALL_PATH)
	cp xow.service $(SERVICE_PATH)
	systemctl enable $(SERVICE_PATH)/xow.service
	systemctl start xow

.PHONY: clean
clean:
	$(RM) xow $(OBJECTS) $(DEPENDENCIES)

-include $(DEPENDENCIES)
