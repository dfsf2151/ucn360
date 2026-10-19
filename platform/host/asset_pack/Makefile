CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES = -I../../../engine
SOURCES = main.cpp ../../../engine/data/bundle.cpp ../../../engine/assets/png.cpp \
          ../../../engine/assets/image_bank.cpp ../../../engine/assets/dxt.cpp ../../../engine/game/mask.cpp
OUT = build/asset_pack

all: $(OUT)

$(OUT): $(SOURCES)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(OUT) $(SOURCES) -lz

clean:
	rm -rf build

.PHONY: all clean
