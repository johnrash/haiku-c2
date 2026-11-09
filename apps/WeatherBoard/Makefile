# Builds Weather Board for arm64 Haiku and its package (milestone M66 (docs/milestones.md)).
#   make            the application, WeatherBoard
#   make package    weatherboard-<version>-arm64.hpkg
# Uses the project's cross tools and sysroot (tools/haiku-arm64-env.sh) and
# libdevice from the jam build.

SHELL := /bin/bash
PROJECT := $(abspath ../..)
ENV := . $(PROJECT)/tools/haiku-arm64-env.sh >/dev/null 2>&1;
GEN := $(PROJECT)/generated.arm64
HOST_TOOLS := $(GEN)/objects/linux/x86_64/release/tools
PACKAGE_TOOL := $(HOST_TOOLS)/package/package
LIBDEVICE := $(GEN)/objects/haiku/arm64/release/kits/device

VERSION := 1.2-1
PACKAGE := weatherboard-$(VERSION)-arm64.hpkg

FLAGS := -O2 -Wall
OBJECTS := WeatherBoard.o WeatherView.o WeatherSensors.o

all: WeatherBoard

WeatherBoard: $(OBJECTS) WeatherBoard.rsrc
	$(ENV) $$CXX -o $@ $(OBJECTS) -L$(LIBDEVICE) -ldevice -lbe \
		$$HAIKU_CXX_LIBS
	$(HOST_TOOLS)/xres -o $@ WeatherBoard.rsrc

WeatherBoard.rsrc: WeatherBoard.rdef icon.rdef
	$(HOST_TOOLS)/rc/rc -o $@ WeatherBoard.rdef icon.rdef

# the vector icon, from the picture's outlines (icon/make-icon.py)
icon.rdef: icon/make-icon.py
	python3 icon/make-icon.py --rdef $@

%.o: %.cpp WeatherView.h WeatherSensors.h
	$(ENV) $$CXX $$HAIKU_CXX_INCLUDES $(FLAGS) -c -o $@ $<

# the stripped binary gets its resources again (strip drops them), then
# its attributes from them
package: WeatherBoard
	$(ENV) haiku_rm_tree pkg; \
	mkdir -p pkg/apps "pkg/data/deskbar/menu/Applications"; \
	cp WeatherBoard pkg/apps/; \
	$$STRIP pkg/apps/WeatherBoard; \
	$(HOST_TOOLS)/xres -o pkg/apps/WeatherBoard WeatherBoard.rsrc; \
	haiku_mimeset pkg/apps/WeatherBoard; \
	ln -s ../../../../apps/WeatherBoard \
		"pkg/data/deskbar/menu/Applications/Weather Board"; \
	cp PackageInfo pkg/.PackageInfo; \
	rm -f $(PACKAGE); \
	(cd pkg && $(PACKAGE_TOOL) create -q ../$(PACKAGE))

clean:
	rm -rf WeatherBoard WeatherBoard.rsrc icon.rdef $(OBJECTS) pkg $(PACKAGE)

.PHONY: all package clean
