#!/usr/bin/env python3
"""
build.py - lift your SSO.EXE to C and build the recomp. Linux and Windows (MSYS2).

    python3 tools/build.py [--game DIR] [--headless] [--debug] [--jobs N]

Steps:
  1. find SSO.EXE (--game, else this folder's parent, else this folder)
  2. fetch pcrecomp into .pcrecomp/ if PCRECOMP_HOME is not set (pinned commit)
  3. undo EXEPACK (pcrecomp tools/drm/unexepack.py) -> work/SSO_unpacked.exe
  4. lift to C (tools/lift_full.py) -> gen/
  5. cmake configure + build -> build/sso (build/sso.exe on Windows)

Nothing from the game is stored in git: gen/ and work/*.exe are generated
from your own copy on your own machine.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCRECOMP_URL = "https://github.com/sp00nznet/pcrecomp.git"
PCRECOMP_REV = "2adc02fec0702933325801aa7b29a7646321857f"   # the revision this project was brought up on


def run(cmd, **kw):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.check_call(cmd, **kw)


def find_exe(game):
    cands = [game] if game else [os.path.dirname(ROOT), ROOT, os.getcwd()]
    for d in cands:
        if d and os.path.isdir(d):
            for n in os.listdir(d):
                if n.upper() == "SSO.EXE":
                    return os.path.join(d, n)
    sys.exit("SSO.EXE not found; pass --game <folder with SSO.EXE and the .GRA/.DAT files>")


# The copy this project was lifted from (title screen: "(c)1990 The Learning Company V 1.02").
KNOWN = {
    "SSO.EXE": "bce6846b816d1aad7fdd76d15d2047afbc3658cb4f98c0623bcba5447ac446c2",
    "TEMPLATE.GRA": "117750cd2b9249e837b52f1cc903d51da96aef6f726f9c42bd22e7c63539b000",
}


def check_versions(game_dir):
    import hashlib
    bad = []
    for name, want in KNOWN.items():
        path = next((os.path.join(game_dir, n) for n in os.listdir(game_dir) if n.upper() == name), None)
        if not path:
            bad.append(f"{name}: missing")
            continue
        got = hashlib.sha256(open(path, "rb").read()).hexdigest()
        if got != want:
            bad.append(f"{name}: sha256 {got}\n      expected {want}")
    if bad:
        print("WARNING: this is not the OutNumbered! v1.02 the recomp was made from:")
        for b in bad:
            print("  " + b)
        print("The build continues, but expect crashes or wrong behaviour.")
    else:
        print("game files: OutNumbered! v1.02 (SSO.EXE and TEMPLATE.GRA match)")


def pcrecomp_home():
    env = os.environ.get("PCRECOMP_HOME") or os.environ.get("PCRECOMP")
    if env and os.path.isdir(os.path.join(env, "tools", "lift")):
        return os.path.abspath(env)
    home = os.path.join(ROOT, ".pcrecomp")
    if not os.path.isdir(os.path.join(home, "tools", "lift")):
        run(["git", "clone", "-q", PCRECOMP_URL, home])
        run(["git", "-C", home, "checkout", "-q", PCRECOMP_REV])
    return home


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", help="folder holding SSO.EXE (default: parent of this repo)")
    ap.add_argument("--headless", action="store_true", help="build without SDL2 (scripted/testing only)")
    ap.add_argument("--debug", action="store_true", help="debug build in build-dbg/")
    ap.add_argument("--jobs", type=int, default=0)
    ap.add_argument("--build-dir", default=None)
    a = ap.parse_args()

    exe = find_exe(a.game)
    print(f"game: {exe}")
    check_versions(os.path.dirname(exe))
    pc = pcrecomp_home()
    os.environ["PCRECOMP_HOME"] = pc

    work = os.path.join(ROOT, "work")
    os.makedirs(work, exist_ok=True)
    unpacked = os.path.join(work, "SSO_unpacked.exe")
    sys.path.insert(0, os.path.join(pc, "tools", "drm"))
    import unexepack
    data = open(exe, "rb").read()
    open(unpacked, "wb").write(unexepack.unpack(data) if unexepack.is_packed(data) else data)
    print(f"unpacked: {unpacked}")

    os.environ["SSO_GAME_DIR"] = os.path.dirname(os.path.abspath(exe))   # TEMPLATE.GRA's puzzle modules
    run([sys.executable, os.path.join(ROOT, "tools", "lift_full.py"), unpacked])

    bdir = a.build_dir or os.path.join(ROOT, "build-dbg" if a.debug else "build")
    cfg = ["cmake", "-S", ROOT, "-B", bdir, f"-DCMAKE_BUILD_TYPE={'Debug' if a.debug else 'Release'}"]
    if shutil.which("ninja") and not os.path.exists(os.path.join(bdir, "CMakeCache.txt")):
        cfg += ["-G", "Ninja"]
    if a.headless:
        cfg += ["-DSSO_SDL=OFF"]
    run(cfg)
    b = ["cmake", "--build", bdir]
    if a.jobs:
        b += ["-j", str(a.jobs)]
    run(b)
    out = os.path.join(bdir, "sso.exe" if os.name == "nt" or sys.platform == "msys" else "sso")
    print(f"\nbuilt: {out}\nrun:   {out} \"{os.path.dirname(exe)}\"")


if __name__ == "__main__":
    main()
