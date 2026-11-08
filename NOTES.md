# Notes

## 2026-11-07 bring-up

### The binary
- `SSO.EXE` (107,277 bytes) is **Microsoft EXEPACK**: header has 0 relocations
  and CS:IP 1901:0012 points at the unpacker (`RB` signature, "Packed file is
  corrupt"). pcrecomp `tools/drm/unexepack.py` gives a 113,232-byte MZ: 2,008
  relocations, entry 0000:0000, SS:SP 1BE8:00E6, header 0x2000.
- `Turbo-C - Copyright (c) 1988 Borland Intl.`; startup `mov dx,13F3h` (DGROUP
  at image 0x13F30). Code ends at image 0x13F14.
- No 8087-emulator sites (`CD 34..3D`): no float code, so Neptune's emulator
  overrides do not apply (`OVERRIDES` is empty).
- Strings name `Midnight Rescue!` (network message): the same engine as the
  other Super Solvers titles.

### Lift (`tools/lift_full.py`, forked from Neptune)
- analyzer: 361 functions; + Borland startup + 5 startup subroutines (3 via the
  Turbo C hook vectors); 199 forced starts over 2 rounds; 34 of 35
  `jmp cs:[bx+..]` switches resolved (237 arms). **568 functions, 0 lift errors.**
- Removed Neptune-only paths: the 0x910 video-driver jump vector and the
  patched-VGA-driver variant (`VARIANT`). SSO has no runtime-patched driver.
- One unresolved call target, `far_13F1_0004` (inside DGROUP: data decoded as code).
- Dispatch-miss feedback found: 0x0CA70 (timer ISR, INT 08h), 0x003B9 (startup
  segment helper used by the file loader; missing it leaked the stack down to
  SP=0006).
- `gen/image_id.h` records the CRC32 + size of the lifted load module; the
  loader warns if the user's `SSO.EXE` differs.

### Runtime changes vs Neptune
- Loader undoes EXEPACK itself (`src/recomp/exepack.c`, port of unexepack.py),
  so the shipped `SSO.EXE` runs directly.
- **Current drive is C:** (INT 21h/19h). On A:/B: the game runs the floppy
  release's disk-swap logic: `fn_0B801` -> `fn_0B822` probes `X:\TLCINFO.DAT`
  (233 bytes, `fn_0B746`) on A: and B: and loops on an "Insert ... Program
  Disk / Then Press Enter" prompt. Data paths default to `A:\TLC\sso\` and
  follow the current drive; file names are mapped to the game folder
  case-insensitively.
- INT 13h reset/verify succeed (the game verifies the drive before saving).
- INT 21h 0Dh (disk reset), 1Bh/1Ch (allocation info: media byte F8 = hard disk).
- Video detection: INT 10h/1A00 says no VGA, AH=12h/BL=10h reports EGA, port
  3BAh polled for Hercules (32K reads, then gives up). The built-in EGA driver
  (`fn_00490`, `fn_00748`..) sets mode 0Dh; text is drawn by `fn_0C49D` through
  the glyph blitter `fn_009C6` (write mode 2).
- The game hooks INT 08h (timer) and INT 09h (keyboard, 0B9D:009F) and also
  polls INT 16h/01h.

### Status
Verified headless on Linux: title screen, story screens, TV-station
animation, Club Member Sign-In (reads `USERS.DAT`), game menu, saved-game
prompt, and play in the SGTV halls (scrolling, Telly, zapping). 0 unhandled
DOS/BIOS calls in that run.

Builds:
- Linux, GCC/Clang, headless and SDL2.
- Windows x64, cross-compiled here with clang 18 + mingw-w64 (UCRT) + static
  SDL2: links and produces a self-contained `sso.exe`; **not yet run on Windows**.

### Open
- Play-test on Windows (window, keyboard, audio).
- Sound: game has Sound/Music options; PC speaker path from Neptune's
  `audio.cpp` is in place, AdLib unverified for this title.
- Audit the rest of the game (rooms, decoder, Drill for Skill, trophies) for
  dispatch misses; run `tools/converge.sh` with longer scripted sessions.
- Leftover Neptune debug hooks in runtime16.c (`SSO_WATCHSS`, `SSO_ENTERLOG`)
  read Neptune DGROUP offsets; harmless, off by default, clean up later.
- Console window on Windows (build is a console app; handy for logs).
