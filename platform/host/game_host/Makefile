CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES = -I../../../engine
SOURCES = main.cpp \
          ../../../engine/data/bundle.cpp \
          ../../../engine/data/program.cpp \
          ../../../engine/game/defs.cpp \
          ../../../engine/game/run.cpp \
          ../../../engine/game/run_objects.cpp \
          ../../../engine/game/run_events.cpp \
          ../../../engine/game/mask.cpp \
          ../../../engine/assets/png.cpp \
          ../../../engine/assets/image_bank.cpp \
          ../../../engine/render/world.cpp
HEADERS = $(wildcard ../../../engine/game/*.h ../../../engine/data/*.h)
LDLIBS = -lz
OUT = build/game_host

all: $(OUT)

$(OUT): $(SOURCES) $(HEADERS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(OUT) $(SOURCES) $(LDLIBS)

clean:
	rm -rf build

.PHONY: all clean
