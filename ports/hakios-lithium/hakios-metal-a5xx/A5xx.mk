# SPDX-License-Identifier: MIT
# Focused A5xx targets. The original A6xx Makefile is retained as reference;
# use `make -f A5xx.mk` for this port. Device probes require G4 separately.
CXX ?= clang++
OUT ?= out/a5xx-host
FLAGS = -std=c++17 -O1 -g -Wall -Wextra -Werror -Iinclude
SAN ?= address,undefined
ifneq ($(SAN),)
FLAGS += -fsanitize=$(SAN) -fno-omit-frame-pointer
endif
TRANSFER = src/pm4_a5xx.cpp src/transfer_a5xx.cpp src/fake_a5xx.cpp
ROOT ?= ../../..
.PHONY: test transfer constants texture ios probe
test: transfer constants texture
transfer:
	mkdir -p $(OUT)
	$(CXX) $(FLAGS) -pthread -DHAKIOS_A5XX_TRANSFER_TEST_STANDALONE $(TRANSFER) tests/a5xx_transfer_test.cpp -o $(OUT)/transfer
	$(OUT)/transfer
constants:
	python3 tools/check-a5xx.py
texture:
	python3 $(ROOT)/scripts/test-hakios-a5xx-texture.py
# Static Darwin arm64 archive contains only new A5xx modules and the original
# generation-independent IOKit transport. AIR/ir3 linkage is a separate build.
HAKIOS ?= /home/jia/ios-mix1-build/hakios-lithium-ios16
SDK ?= $(HAKIOS)/userland-sdk/sdks/iPhoneOS16.5.sdk
IOS_CXX = $(HAKIOS)/xnu-deps/host/bin/xnu-clang++ -arch arm64 -isysroot $(SDK) -miphoneos-version-min=16.0
IOS_MODULES = pm4_a5xx transfer_a5xx fake_a5xx texture_a5xx compute_a5xx air_dispatch_a5xx render_a5xx link_a5xx link pm4 gpu iokit_transport backend_a5xx
IOS_ARCHIVE = out/a5xx-ios/libhakios-metal-a5xx-prepared.a
ios: $(IOS_ARCHIVE)
$(IOS_ARCHIVE): A5xx.mk $(foreach module,$(IOS_MODULES),src/$(module).cpp) $(wildcard include/hakios-metal/*.hpp)
	mkdir -p out/a5xx-ios/obj
	set -e; for module in $(IOS_MODULES); do $(IOS_CXX) -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude -c src/$$module.cpp -o out/a5xx-ios/obj/$$module.o; done
	llvm-ar --format=darwin rcs out/a5xx-ios/libhakios-metal-a5xx-prepared.a $(foreach module,$(IOS_MODULES),out/a5xx-ios/obj/$(module).o)
probe: out/a5xx-ios/hakios-metal-probe-a5xx
out/a5xx-ios/hakios-metal-probe-a5xx: tools/probe-a5xx.cpp $(IOS_ARCHIVE)
	clang++ -target arm64-apple-ios16.5 -isysroot $(SDK) --ld-path=/usr/bin/ld64.lld-18 -Wl,-platform_version,ios,16.5,16.5 -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tools/probe-a5xx.cpp $(IOS_ARCHIVE) -framework IOKit -framework CoreFoundation -o $@
