Ultimate Custom Night - Xbox 360 homebrew port (default.xex)
============================================================

An unofficial fan port of Ultimate Custom Night to the Xbox 360 (RGH/JTAG), launched from Aurora.
It runs the game's own event logic through a C++ re-implementation of the Clickteam Fusion runtime.
The game data is used unchanged. Sound, saves and 60 game ticks per second are all there.

Not affiliated with or endorsed by Scott Cawthon or Clickteam. Ultimate Custom Night and its characters,
art and sounds belong to their owners. Please support the original game.

Install
-------
1) Copy the whole ucn360 folder to the console (HDD, or a FAT32 USB drive), over FTP or directly.
2) In Aurora, add the folder as a game path or open it from the file manager, then launch default.xex.
3) Keep these files next to default.xex:
   bundle.bin    game data (frames, events, objects)
   images.pak    images (DXT, 152 MB)
   masks.pak     collision masks
   sounds.pak    sounds (loaded fully into memory, 110 MB)
   fonts.pak     fonts
   loading.rgba  loading screen
   pause.rgba    pause menu
   ucn_save.dat  save file (empty until the first save)

Controls (from the game's own control map)
------------------------------------------
   Left stick              Move the pointer (mouse)
   A                       Click
   D-pad Left / Right      Close left door / right door        (A / D)
   D-pad Up / Down         Close forward vent / side vent      (W / F)
   Y                       Monitor                             (S)
   B                       Flashlight                          (Z)
   X                       Desk fan on/off                     (Space)
   RB                      Catch fish (Old Man Consequences)   (C)
   RT                      All off                             (X)
   LB                      Enter (close El Chip's ad)
   Start                   In the office: pause menu (Resume / Controls / Quit to menu)
                           Elsewhere: Enter (PRESS START, leaving Game Over)
   Back (hold)             Esc - return to the menu
   LT + D-pad Up/Right/Down/Left, LT + X, LT + Y
                           Keys 1-6: power generator, silent ventilation, heater,
                           power AC, global music box, all off
   D-pad with the monitor up
                           Switch to the next camera in that direction
   R3 (click right stick)  Show / hide the button guide over the game
   LT + RT, hold 3 s       On the menu only: erase save data (a bar fills, then turns red)

Pointer:
   It shows when you move the left stick or press A, and hides after 3 seconds without movement.
   To raise a character's level, rest the pointer on their portrait so the two arrows appear.
   Then point at the up arrow and press A (the down arrow lowers it).

Saves
-----
   Points, high score and best times are saved to ucn_save.dat next to default.xex.
   If that folder is read-only (Xenia, for example), the save goes to cache:\ucn_save.dat instead.

Log
---
   The title writes default.log next to itself (or in cache:). If something breaks, please include it in your report.

Status
------
   - The engine is checked against the original game, tick by tick.
     15 of 16 frames match the live game's state exactly, and an office scenario with every control
     matches for 1500 ticks.
   - In Xenia: it boots and loads everything, sound works, the logic runs at 60 ticks/s, and the office
     draws at 48-60 frames/s.
   - Not yet tested on real hardware.
     Rendering is done on the CPU across all 6 hardware threads. If it is slow on a console, the game
     logic still runs at a fixed 60 ticks/s; only the drawn frame rate drops.
