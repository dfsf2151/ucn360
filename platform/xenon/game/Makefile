# UCN 360, the playable title: make -j8 XSDK=../../../third_party/xsdk
XSDK ?= ../../../third_party/xsdk
TOOLCHAIN ?= $(HOME)/xsdk-gcc/bin/powerpc-xsdk-elf-
CXX := $(TOOLCHAIN)g++
PYTHON ?= python3
ENGINE := ../../../engine

BUILD := build
# -Wa,-I: gpu_draw.cpp's .incbin takes the sprite shaders' SPIR-V from xost's test inputs
CXXFLAGS := -mcpu=cell -fno-pic -std=gnu++17 -O2 -g -Wall -I$(ENGINE) -I$(XSDK)/include -MMD -MP \
	-Wa,-I$(XSDK)/xenos-toolchain/xsc/testdata
LDFLAGS := -static -nostdlib -T $(XSDK)/src/xsdk.ld -Wl,-Map,$(BUILD)/ucn360.map
LIBS := -Wl,--start-group $(XSDK)/build/libxsdk.a -lstdc++ -lc -lm -lgcc -Wl,--end-group

SOURCES := main.cpp gpu_draw.cpp $(ENGINE)/data/bundle.cpp $(ENGINE)/data/program.cpp $(ENGINE)/game/defs.cpp \
	$(ENGINE)/game/run.cpp $(ENGINE)/game/run_objects.cpp $(ENGINE)/game/run_events.cpp $(ENGINE)/game/mask.cpp \
	$(ENGINE)/assets/dxt.cpp $(ENGINE)/render/world.cpp
OBJECTS := $(patsubst %.cpp,$(BUILD)/obj/%.o,$(notdir $(SOURCES)))
vpath %.cpp . $(ENGINE)/data $(ENGINE)/game $(ENGINE)/assets $(ENGINE)/render

all: $(BUILD)/default.xex

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(BUILD)/obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/ucn360.elf: $(OBJECTS) $(XSDK)/build/libxsdk.a $(XSDK)/src/xsdk.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

$(BUILD)/default.xex: $(BUILD)/ucn360.elf $(XSDK)/tools/elf2xex.py
	$(PYTHON) $(XSDK)/tools/elf2xex.py $< $@

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)
.PHONY: all clean
