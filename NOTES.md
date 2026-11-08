# Notes

## TODO (next session, 2026-11-08)

1. **High CPU use: fans spin up while playing.** *(Partly done 2026-11-08: guest
   capped at 25 MHz-equivalent (486DX-33 class), 100% -> ~4% of a core at the title screen. Still
   to do: compile out per-function debug hooks; idle-poll sleeps if still needed.)* The game busy-waits like any DOS
   program (INT 16h/01h "key ready?" polls, video-status port polls, loops on its
   timer counter), and the lifted code spins at host speed, so one core sits near
   100%. Fix: sleep ~1 ms on repeated empty INT 16h/01h polls, pace video-status
   (3DAh) polls to a 60 Hz frame, sleep until the next PIT tick when RECOMP_TICK
   finds a loop spinning on the timer, and compile the per-function debug hooks
   (RECOMP_TRACE / recomp_enter) out of release builds. Measure CPU use before and
   after at the title screen and in the halls. Game speed must not change.
2. First Windows play-test (window, keyboard, sound: try both --tandy and --speaker).

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
- Other 10 puzzle modules are lifted but only Prize is Right is verified. Audit the
  rest of the game (decoder, Drill for Skill, trophies) for
  dispatch misses; run `tools/converge.sh` with longer scripted sessions.
- Leftover Neptune debug hooks in runtime16.c (`SSO_WATCHSS`, `SSO_ENTERLOG`)
  read Neptune DGROUP offsets; harmless, off by default, clean up later.
- Console window on Windows (build is a console app; handy for logs).

## Puzzle pictures: code modules in TEMPLATE.GRA (fixed)

Symptom: the picture box on every puzzle screen stayed black. A trace showed
`[disp] MISS 4F45:0000` right after the game read 0x1158 bytes of
TEMPLATE.GRA (file offset 0x1FC9) into 4F45:0000 and far-called it.

- TEMPLATE.GRA holds 11 separately compiled puzzle modules (one per stand
  type), each followed by its picture data. Each starts `push bp / mov bp,sp /
  push ds / mov ax,cs / mov ds,ax` (CS = DS = load segment) and calls back
  into the game with `push n / call 2Ch`, a stub that does `int 60h`.
- INT 60h handler 0C9A:0002 (image 0xC9A2) pops the frame into cs:[4A..4E],
  far-calls `[DGROUP:4BF0 + 4*AX]` (27 services), pushes the frame back, iret.
  Neither the handler nor the services are reachable by call-graph analysis;
  `lift_full.py` now adds them as function starts.
- `lift_full.py` lifts the modules position-independently (`_CODE_SEG=None`, so
  `cpu->cs` is the live load segment) as `tm_<file offset>` into
  `gen/tmpl_000.c`: recursive descent from each module's entry (69 functions).
  Needs the game folder (`SSO_GAME_DIR`; build.py sets it).
- Runtime: reads from TEMPLATE.GRA are recorded (memory range -> file
  offset); `recomp_dispatch` routes a call into such a range to
  `recomp_dispatch_tm(file offset)` with CS = the call's segment. Misses there
  go to `work/sso_tmisses.txt` and are fed back on the next lift.
- Runtime: `int n` for a vector the game installed now runs the guest handler
  (push FLAGS/CS/IP, dispatch, drop the frame since lifted `iret` doesn't).

Verified: Game room -> Prize is Right puzzle draws (prizes, tags, calculator),
answer 79 accepted, score 300 -> 600.

## Sound (2026-11-08)

- EGA mode sound is the PC speaker (61h, PIT channel 2); already modelled.
- The game also has a Tandy 1000 / PCjr path: TI SN76496 on port C0h (3 square
  voices + noise). It is chosen from one byte, DGROUP:[4C62h] = 21h (Tandy BIOS
  signature F000:C000, set by `fn_0CB1A`), which drives four decisions:
  - `fn_01B29`: music bank in MOCKET.DAT: 15E0h at 0x2450 (Tandy, 4 voices with
    volume envelopes, 150 `5D xx` set-envelope commands) vs 15E1h at 0x5AC8 (speaker).
  - `fn_01B81` -> `fn_0C2F3(tandy)`: sound driver init; sets `[4356h]` (driver) and
    `[4358h]` (voices: 4 Tandy, 1 speaker).
  - `fn_0BA41`/`fn_0BA6F`: keyboard handler (Tandy scan codes) - must stay PC.
  - `fn_0CB1A`/`fn_0CBD5`: video detection - must stay EGA.
- `lift_full.py` SOUND_MACHINE_READS routes only the first two through
  `sso_sound_machine_byte()` (21h with Tandy sound, the real byte otherwise).
  Default Tandy; `--speaker` / `SSO_SOUND=speaker` for the PC speaker.
- First attempt only overrode the driver init: the Tandy driver then played the
  speaker bank, whose songs never set an envelope, so the envelope pointer stayed 0
  and volumes were read from DGROUP:0004 ("Turbo-C - Copyright..."), e.g. 90h+'T'
  = E4h = a noise-control write. That was the "two tracks at once" sound.
- `audio.cpp`: SN76496 at 3.579545 MHz (freq = clock / 32N, 15-bit LFSR noise, 2 dB
  steps), writes timestamped and applied at the exact sample like the speaker.
  Tandy audio mux: port 61h bits 5-6 (00 = PIT tone, 11 = SN76496) gate which
  source is heard.
- Runtime: timer ticks are no longer delivered while the guest has IF clear
  (like the CPU); INT 60h shim restores IF on return (as iret would).
- Diagnostics: startup prints the SDL audio driver and output devices; the
  window title shows the sound device, output level (snd N%) and frames/s the
  device pulls (0 = device not running). `SSO_AUDIOLOG=1` logs speaker and chip
  writes; `SSO_MEMDUMP_MS=n` writes guest memory to work/mem.bin once.

## Guest speed cap (2026-11-08)

- `--mhz=N` / `SSO_MHZ=N` (default 25 since play-testing felt identical; 0 = unlimited): the guest runs at most N
  million original x86 instructions per second; ahead of the wall clock, the
  runtime sleeps (`pace_guest()` in recomp_tick, sleeps when >2 ms ahead, resets
  its base when >50 ms behind so stalls are not "caught up").
- Counting: lift_full.py wraps lift16's Lifter so each loop back-edge emits
  `RECOMP_TICKN(cpu, n)` (n = instructions in the loop body) and each function
  entry `RECOMP_COUNT(len)`. An estimate (early returns overcount, calls into
  straight-line code count once per call), not cycles.
- Measured at the title screen over 15 s: uncapped ~740 M instr/s at 100% of a
  core; capped at 100: ~15% of a core; at 25: ~4%; at 10: ~2%; identical frames. Game timing
  is driven by the timer interrupt, so play speed is unchanged.
- The window title shows the measured guest speed (guest N MHz).
