# See mt76.h for possible channels
BUILD := DEBUG
CHANNEL := 165
VERSION := $(shell git describe --tags)

FLAGS := -Wall -Wpedantic -std=c++17 -MMD
DEBUG_FLAGS := -Og -g -DDEBUG
RELEASE_FLAGS := -O3
MACROS := -DCHANNEL=$(CHANNEL) -DVERSION=\"$(VERSION)\"

CXXFLAGS += $(FLAGS) $($(BUILD)_FLAGS) $(MACROS)
LDLIBS += -lstdc++ -lusb-1.0 -lpthread
SOURCES := $(wildcard *.cpp) $(wildcard */*.cpp)
OBJECTS := $(patsubst %.cpp,%.o,$(SOURCES)) firmware.o
DEPENDENCIES := $(OBJECTS:.o=.d)

INSTALL_PATH := /usr/local/bin
SERVICE_PATH := /etc/systemd/user

xow: $(OBJECTS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

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
