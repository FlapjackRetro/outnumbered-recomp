# Super Solvers: OutNumbered! (DOS) recomp

Static recompilation of The Learning Company's 1990 DOS game *Super Solvers:
OutNumbered!* (`SSO.EXE`, v1.02) to native C, using the
[pcrecomp](https://github.com/sp00nznet/pcrecomp) toolbox. Runs natively on
**Linux and Windows** (no DOSBox, no emulator): the game's 16-bit x86 code is
lifted to C and linked against a small DOS/BIOS/EGA/sound runtime on SDL2.

Forked from [operation-neptune-recomp](https://github.com/FlapjackRetro/operation-neptune-recomp):
same publisher, same Turbo C 2.0 toolchain, same runtime.

Game files live one folder up (`../`). They are **not** part of this project
and must never be committed (see `.gitignore`).

## Target

| | |
|---|---|
| Binary | `SSO.EXE`, 16-bit DOS MZ, 107,277 bytes, v1.02 |
| Packing | Microsoft **EXEPACK** (undone at build time and again at load time) |
| Unpacked | 113,232 bytes, 2,008 relocations, entry 0000:0000, DGROUP 0x13F3 |
| Toolchain | Turbo C 2.0 (Borland 1988 runtime), medium/large model, no floating point |
| Video | built-in EGA driver, mode 0Dh (320x200x16); `sso.cga`/`sso.tga` are the CGA/Tandy drivers |
| Data | `MOCKET.DAT`, `*.GRA`, `HELP.DAT`; saves in `USERS.DAT`, `HISCORE.DAT` |
| Pipeline | `unexepack` -> `analyze` + `largemodel16` -> `lift16` -> runtime |

## Supported version

Only this release is supported (the title screen reads **© 1990 The Learning
Company V 1.02**). The lifted code matches one exact executable, so check your
copy before building:

| File | Size | SHA-256 | MD5 | CRC32 |
|---|---|---|---|---|
| `SSO.EXE` | 107,277 | `bce6846b816d1aad7fdd76d15d2047afbc3658cb4f98c0623bcba5447ac446c2` | `c637a8f9eba7af5c6a77e2f4d00e5b3c` | `69D34425` |
| `TEMPLATE.GRA` | 109,433 | `117750cd2b9249e837b52f1cc903d51da96aef6f726f9c42bd22e7c63539b000` | | |
| `MOCKET.DAT` | 36,140 | `324abae88ed2639c36ab3c25af1e13f34d1a3064737beb18e5a0f7388657b416` | | |

`SSO.EXE` is EXEPACK-compressed; `TEMPLATE.GRA` holds the puzzle-picture code
the build also lifts; `MOCKET.DAT` holds the music. `tools/build.py` checks
`SSO.EXE` and `TEMPLATE.GRA` and warns on a mismatch. `USERS.DAT` and
`HISCORE.DAT` are saves and differ between copies.

Linux: `sha256sum SSO.EXE` · Windows: `Get-FileHash SSO.EXE` (PowerShell).

## Build

You need your own copy of the game. Put this repo in a `recomp` folder inside
the game folder (next to `SSO.EXE`), or pass `--game`.

**Linux** (needs `python3`, `git`, `cmake`, a C/C++ compiler, SDL2 dev package,
e.g. `sudo apt install build-essential cmake ninja-build libsdl2-dev`):

```
python3 tools/build.py              # or: python3 tools/build.py --game /path/to/Outnumbered
./build/sso ..                      # game folder; defaults to . then ..
```

**Windows**: double-click `install.bat` (or
`powershell -ExecutionPolicy Bypass -File install.ps1 -GameDir C:\path\to\Outnumbered`).
It installs MSYS2 + toolchain + SDL2 if missing, fetches pcrecomp, lifts your
`SSO.EXE`, builds `build\sso.exe` and writes `play.bat`.

Manual Windows build from an MSYS2 MINGW64 shell works the same as Linux:
`python tools/build.py --game /c/path/to/Outnumbered`.

`tools/build.py` fetches pcrecomp into `.pcrecomp/` (pinned revision) unless
`PCRECOMP_HOME` points at a checkout. `--headless` builds without SDL2.

## Running

```
sso [options] [game-dir]     # finds SSO.EXE in game-dir (default: . then ..)
sso [options] SSO.EXE [game-dir]

  --tandy      Tandy 3-voice sound (default)
  --speaker    PC speaker
  --mhz=N      guest speed cap, million instructions/s (default 25, 0 = unlimited)
```

Sound: `--tandy` (default) uses the game's own Tandy 1000 3-voice music driver
(emulated TI SN76496) with the EGA graphics; `--speaker` is the original PC
speaker.

The original, still-packed `SSO.EXE` is fine: the loader undoes EXEPACK and
checks the image against the one the build was lifted from.

Keyboard as on DOS (arrows, Enter, Space, Tab, Esc, F1-F3). The game writes its
saves (`USERS.DAT`, `HISCORE.DAT`) back into the game folder.

Debug environment variables (all optional): `SSO_TRACE=1` (DOS/BIOS calls and
dispatch misses), `SSO_SECONDS=n` (watchdog), `SSO_FRAME=out.ppm` (screen at
exit), `SSO_FRAMES=prefix` + `SSO_FRAMES_MS`, `SSO_KEYSEQ="enter@3000,up@5000"`
and `SSO_RAWSEQ="1C@3000,9C@3100"` (scripted input), `SSO_SOUND=speaker|tandy`, `SSO_MHZ=N`,
`SSO_WAV=out.wav`.

## Development loop

```
tools/converge.sh <game-dir>     # lift -> build -> run until no new dispatch misses
```

The runtime records addresses it could not dispatch to `work/sso_misses.txt`;
the lifter turns them into function starts on the next run. That file is
committed so a fresh build is complete without a discovery run.

See `NOTES.md` for findings.

## Licence and credits

Original code here is MIT (see `LICENSE`). **Super Solvers: OutNumbered! is the
copyright of The Learning Company**; no game files are included and you must
supply your own copy. Third-party credits (pcrecomp, ymfm, SDL2, build tools)
are in `NOTICE`.
