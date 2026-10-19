# UCN 360 — milestone 0 smoke title. Builds against a checkout of xsdk:
#   make XSDK=../../../third_party/xsdk
XSDK ?= ../../../third_party/xsdk
TOOLCHAIN ?= $(HOME)/xsdk-gcc/bin/powerpc-xsdk-elf-
CC := $(TOOLCHAIN)gcc
PYTHON ?= python3

BUILD := build
CFLAGS := -mcpu=cell -fno-pic -std=gnu17 -O2 -g -Wall -Wextra -I$(XSDK)/include
LDFLAGS := -static -nostdlib -T $(XSDK)/src/xsdk.ld -Wl,-Map,$(BUILD)/ucn360_m0.map
LIBS := -Wl,--start-group $(XSDK)/build/libxsdk.a -lstdc++ -lc -lm -lgcc -Wl,--end-group

all: $(BUILD)/ucn360_m0.xex

$(BUILD)/ucn360_m0.elf: m0_smoke.c $(XSDK)/build/libxsdk.a $(XSDK)/src/xsdk.ld
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ m0_smoke.c $(LIBS)

$(BUILD)/ucn360_m0.xex: $(BUILD)/ucn360_m0.elf $(XSDK)/tools/elf2xex.py
	$(PYTHON) $(XSDK)/tools/elf2xex.py $< $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
