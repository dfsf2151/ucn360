CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES = -I../../../engine
SOURCES = main.cpp ../../../engine/data/bundle.cpp ../../../engine/data/program.cpp
OUT = build/bundle_dump

all: $(OUT)

$(OUT): $(SOURCES)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(OUT) $(SOURCES)

clean:
	rm -rf build

.PHONY: all clean
