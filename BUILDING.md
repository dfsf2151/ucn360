# Building UCN 360 from source

This repository holds the port's own code only: the engine, the console and PC front ends, the tools and the
design notes. It holds none of Ultimate Custom Night's data (images, sounds, the Clickteam application, the
HTML5 runtime). Those belong to their owners; you need your own copy of the game to build the data files.

## Layout

- `engine/`: a C++ re-implementation of the Clickteam Fusion runtime the HTML5 export uses.
  - `data/`: the bundle reader.
  - `game/`: objects, events, expressions, collision masks and pad helpers.
  - `render/`: the draw list and a CPU rasteriser, used by the PC tools.
  - `assets/`: PNG and DXT.
- `platform/xenon/game/`: the Xbox 360 title (`default.xex`): pad, audio, saves, and the GPU renderer
  (`gpu_draw.cpp`).
- `platform/host/`: PC tools.
  - `game_host` runs a frame tick by tick and writes its state, to diff against recordings of the live game.
  - `frame_render` draws a frame to PNG.
  - `asset_pack` builds `images.pak` and `masks.pak`.
- `tools/`: scripts.
  - `compile_bundle.py` builds `bundle.bin`.
  - `sound_pack.py` and `font_pack.py` build `sounds.pak` and `fonts.pak`.
  - `loading_screen.py`, `pause_menu.py`, `pad_map.py` and `press_start.py` make the console's own screens.
  - The rest are the parity checks against the browser game (`m3_parity.sh`, `compare_state.py`).
- `docs/`: design notes and the runtime map.

## The console title

Needs xsdk, a homebrew Xbox 360 SDK (MIT, by Mena), at the commit in `docs/xsdk-pin.txt`. The Makefile looks
for it at `third_party/xsdk`. It also needs xsdk's toolchain (GCC 15 for `powerpc-xsdk-elf`) at
`~/xsdk-gcc`.

    make -C platform/xenon/game -j8      # -> platform/xenon/game/build/default.xex

The sprite shaders come from xsdk's own test inputs (`xenos-toolchain/xsc/testdata`) and compile on the
console at start.

## The PC tools

    make -C platform/host/game_host
    make -C platform/host/frame_render
    make -C platform/host/asset_pack

The game data files (`bundle.bin`, `images.pak`, `masks.pak`, `sounds.pak`, `fonts.pak`) come from your copy of
the game's HTML5 export, through the tools above.
