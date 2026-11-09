# Builds LCD Shield for arm64 Haiku and its package (PLAN.md M68).
#   make            the application, LCDShield
#   make package    lcdshield-<version>-arm64.hpkg
# Uses the project's cross tools and sysroot (tools/haiku-arm64-env.sh),
# libdevice and libbnetapi from the jam build, the fork's lcd_shield.h, and
# Weather Board's sensor code (../WeatherBoard).

SHELL := /bin/bash
PROJECT := $(abspath ../..)
ENV := . $(PROJECT)/tools/haiku-arm64-env.sh >/dev/null 2>&1;
GEN := $(PROJECT)/generated.arm64
HOST_TOOLS := $(GEN)/objects/linux/x86_64/release/tools
PACKAGE_TOOL := $(HOST_TOOLS)/package/package
LIBDEVICE := $(GEN)/objects/haiku/arm64/release/kits/device
WEATHER := ../WeatherBoard

VERSION := 1.5-2
PACKAGE := lcdshield-$(VERSION)-arm64.hpkg

FLAGS := -O2 -Wall -I$(PROJECT)/haiku/headers/private/drivers -I$(WEATHER)
OBJECTS := LCDShield.o ShieldWindow.o ShieldBox.o ShieldPanel.o \
	LcdFont.o ShieldDevice.o Snapshot.o WeatherSensors.o

all: LCDShield

LCDShield: $(OBJECTS) LCDShield.rsrc
	$(ENV) $$CXX -o $@ $(OBJECTS) -L$(LIBDEVICE) -ldevice -lbnetapi \
		-lnetwork -lbe $$HAIKU_CXX_LIBS
	$(HOST_TOOLS)/xres -o $@ LCDShield.rsrc

LCDShield.rsrc: LCDShield.rdef icon.rdef
	$(HOST_TOOLS)/rc/rc -o $@ LCDShield.rdef icon.rdef

icon.rdef: icon/make-icon.py
	python3 icon/make-icon.py --rdef $@

%.o: %.cpp LcdFont.h ShieldBox.h ShieldDevice.h ShieldPanel.h ShieldWindow.h Snapshot.h
	$(ENV) $$CXX $$HAIKU_CXX_INCLUDES $(FLAGS) -c -o $@ $<

WeatherSensors.o: $(WEATHER)/WeatherSensors.cpp $(WEATHER)/WeatherSensors.h
	$(ENV) $$CXX $$HAIKU_CXX_INCLUDES $(FLAGS) -c -o $@ $<

# the stripped binary gets its resources again (strip drops them), then
# its attributes from them
package: LCDShield
	$(ENV) haiku_rm_tree pkg; \
	mkdir -p pkg/apps "pkg/data/deskbar/menu/Applications"; \
	cp LCDShield pkg/apps/; \
	$$STRIP pkg/apps/LCDShield; \
	$(HOST_TOOLS)/xres -o pkg/apps/LCDShield LCDShield.rsrc; \
	haiku_mimeset pkg/apps/LCDShield; \
	ln -s ../../../../apps/LCDShield \
		"pkg/data/deskbar/menu/Applications/LCD Shield"; \
	cp PackageInfo pkg/.PackageInfo; \
	rm -f $(PACKAGE); \
	(cd pkg && $(PACKAGE_TOOL) create -q ../$(PACKAGE))

clean:
	rm -rf LCDShield LCDShield.rsrc icon.rdef $(OBJECTS) pkg $(PACKAGE)

.PHONY: all package clean
