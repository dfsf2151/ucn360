CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES = -I../../../engine
SOURCES = main.cpp \
          ../../../engine/data/bundle.cpp \
          ../../../engine/assets/png.cpp \
          ../../../engine/assets/image_bank.cpp \
          ../../../engine/render/renderer.cpp \
          ../../../engine/render/world.cpp \
          ../../../engine/data/program.cpp \
          ../../../engine/game/defs.cpp \
          ../../../engine/game/run.cpp \
          ../../../engine/game/run_objects.cpp \
          ../../../engine/game/run_events.cpp \
          ../../../engine/game/mask.cpp \
          ../../../engine/assets/image_pack.cpp \
          ../../../engine/assets/dxt.cpp
LDLIBS = -lz
OUT = build/frame_render

all: $(OUT)

$(OUT): $(SOURCES)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(OUT) $(SOURCES) $(LDLIBS)

clean:
	rm -rf build

.PHONY: all clean
