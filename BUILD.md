# Building H2Classic

H2Classic was developed in a Linux workspace, with Wine running the Xbox SDK's own compiler and zig and LLVM building
the Windows layer. The scripts still carry that workspace's absolute paths (`/home/claude/...`). Recreate the layout
below, or change the paths at the top of each script. The per-build developer notes are in [DEVLOG.txt](DEVLOG.txt),
newest first. Its section "For whoever continues this work" is the long version of this file.

## What you need (none of it is in this repository)

| What | Where the scripts expect it |
| --- | --- |
| The Xbox SDK the game was built with (XDK 5849: `bin/vc71` with CL 13.10.3077, `include`, `lib`). You must obtain it lawfully yourself | `/home/claude/sdk/xbox` |
| Your own unmodified Halo 2 v1.0 `default.xbe` (4,861,952 bytes, SHA-256 `03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`) | `/home/claude/xbe/default.xbe` |
| For game version 1.5: your own `default.xbe` of the last title update (English, 4,919,296 bytes, CRC-32 `a5b0b715`) | `/home/claude/v15/default_v15.xbe` |
| The [halo2-decompiled](https://github.com/kirklandsig/halo2-decompiled) source at the snapshot of 18 November 2026 (its README: 10,707 of 11,326 game functions matched), with `decomp_patches/h2classic_decomp.patch` applied, and its own `orig/default.xbe` in place | `/home/claude/h2decomp_src/halo2-decompiled-main` |
| Linux with 32-bit Wine (`wine32:i386`; Wine 9 was used), python3 with `ziglang` (zig 0.17.1), `capstone`, `unicorn` and `Pillow`, and LLVM 18's `lld-link`, `llvm-nm` and `llvm-ar` | |
| Only for the DLSS helper: NVIDIA's DLSS SDK (`include/` and `lib/Windows_x86_64/x64/nvsdk_ngx_d.lib`). `dlss/build_helper.sh` shows how to fetch just those | `/home/claude/ext/DLSS` |

## Layout

| From this repository | To |
| --- | --- |
| `scripts/` | `/home/claude/scripts/` |
| `scripts15/` (game version 1.5's part of the pipeline) | `/home/claude/v15/` |
| `tools15/` (small tools used while working: disassembly, checks, the menu driver `nav.py`) | `/home/claude/v15tools/` |
| `tools/xdkw/` (`CL.Exe`, `Link.Exe`, `Lib.Exe`: shell scripts that run the SDK's tools under Wine; `chmod +x`) | `/home/claude/xdkw/bin/vc71/`, with `/home/claude/xdkw/include` and `/home/claude/xdkw/lib` as symlinks into the SDK |
| `tools/*.sh` | `/home/claude/native/` |
| `layer3/` (the Windows layer; `layer2/` in earlier versions of the repository is the same layer before it served both game versions - delete it) | `/home/claude/native/layer3/` |
| `launcher/`, `dlss/`, `res/` | `/home/claude/native/launcher/`, `/home/claude/native/dlss/`, `/home/claude/native/res/` |
| `generated/*.json` | `/home/claude/native/` |
| `generated/retail_syms.S`, `generated/retail_code.S`, `generated/retail_tables.template.S` | `/home/claude/native/layer/` |
| `generated15/retail_syms.S`, `generated15/retail_code.S` (1.5's; `scripts15/tables15.py` makes 1.5's `retail_tables.S`) | `/home/claude/native15/layer/` |
| `generated/layer_lib/*.a` (empty archives for the `/DEFAULTLIB` names the game's objects carry) | `/home/claude/native/layer/lib/` |

After copying, make the scripts executable again: `chmod +x /home/claude/native/*.sh /home/claude/native/dlss/build_helper.sh
/home/claude/xdkw/bin/vc71/*` (a download from GitHub's web page does not keep that).

`/home/claude/native/mingw/` holds the mingw-w64 start-up and import libraries that zig 0.17.1 builds for
`x86-windows-gnu` (`crt2.obj`, `libmingw32.lib`, `zigc.lib`, `compiler_rt.lib`, `ubsan_rt.lib`, `advapi32.lib`,
`kernel32.lib`, `ntdll.lib`, `shell32.lib`, `user32.lib`, `ws2_32.lib`, `api-ms-win-crt-*.lib`). To get them, link any
small C program with `ZIG_VERBOSE_LINK=1 python3 -m ziglang cc -target x86-windows-gnu -lws2_32 -ladvapi32 -lshell32
-luser32 -lntdll`, then copy the files that the printed link line names out of zig's cache.

## Wine, once per session

Do this before the first compile. Otherwise the first tool run hangs on Wine's background services:

    export WINEDEBUG=-all WINEPREFIX=/home/claude/.wine32 XDK_DIR=/home/claude/xdkw
    setsid wineserver -p </dev/null >/dev/null 2>&1 &
    setsid wine cmd /c "echo warm" </dev/null >/dev/null 2>&1 &

## The data the program needs from your XBE

When the program starts, it puts the XBE's data at the console's addresses. It also writes what the game's 407 C++
start-up initialisers leave in memory, and those bytes come from the XBE. So they are not published here. Make them
from your own copy:

    python3 scripts/emuinit.py /home/claude/xbe/default.xbe <decomp>/config/functions.csv   # -> native/init_delta.json
    python3 scripts/make_retail_tables.py                                                   # -> native/layer/retail_tables.S

`emuinit.py` runs the initialisers in Unicorn over your XBE: 702 runs, 2,687 bytes. `make_retail_tables.py` puts them
into `retail_tables.template.S`. The template holds the sections to load (with their CRCs) and the code pointers in
the game's data, already cleaned up as `fix_resolve.json` records. With the right XBE, the result is byte for byte
the file the published builds used.

## Compiling the game

The game's objects are not published either. Make them from the decompilation:

1. Apply the patch to the decompilation (see [decomp_patches/README.md](../decomp_patches/README.md)).
2. In the decompilation, with `XDK_DIR` set, run its own build: `python3 tools/build.py` and `python3 tools/check.py`.
   They leave `build/gen/<stem>_tu.cpp`, which the next step compiles, so that vtables, deleting destructors and
   cross-file inlines are emitted.
3. Compile every source as an ordinary object, without link-time code generation:
   `python3 scripts/native_compile.py <decomp> /home/claude/native/obj2 <jobs>`, then
   `ls /home/claude/native/obj2/*.obj > /home/claude/native/objects2.rsp`.
4. Rewrite the objects: `python3 scripts/add_globals.py`, then `python3 scripts/rewrite.py`. The first adds data
   symbols that newly compiled source defines for retail data (listed in `extra_globals.json`) to the plan. The
   second makes `native/obj_r/` and `objects_r.rsp`, where the data symbols that stand for retail data become
   references to the retail addresses.
5. Put the anti-aliasing view hook into one object again (`rewrite.py` remakes it):
   `python3 scripts/scene_hook.py obj_r/unknown_023540.obj obj_r/unknown_023540.obj`.

`tools/recompile.sh <build id> <object stem>...` does steps 3-5 and the link for sources that changed (1.0-only link;
for the program of both versions run the 1.5 steps below and `build3.sh` after steps 3-5).

The tables in `generated/` (`plan.json`, `fixups2.json`, `statics_plan.json`, `vtables_plan.json`, the route lists
and the rest) are the ones the published builds were made with. Make them again only after the decompilation itself
has changed (new matched functions): DEVLOG.txt, "The full path", gives the order (`oracle.py`, `undef3.py`,
`plan.py`, `oracle2.py`, `scan.py`, `scan2.py`, `statics1.py`, `statics2.py`, `vtables.py`, `gen.py`,
`gen_code.py`). Two inputs of that path are not published because they are names from the SDK's own libraries:
`libsig.json` (`scripts/libsig.py`, from your SDK's libraries) and the `libnames` part of `oracle2.json`
(`scripts/oracle2.py`).

## Linking the program

    cd /home/claude/native && ./build3.sh <build id> > /tmp/build.log 2>&1; echo "build rc=$?"

Always check the exit code: a failed build leaves the previous exe in `out3/`. The result is `out3/halo2_native.exe`
(released as `H2Classic.exe`), with `out3/halo2_native.map`. The script prints both ends of the block reserved for the
console's image, and they must read `0x11000` and `0x586000`.

How `build3.sh` puts the program together (`build2.sh` is the earlier script for a 1.0-only program):

- The `.c` files of `layer3/` are compiled with `zig cc -target x86-windows-gnu`. `xd3d.cpp`, `xdsound.cpp`,
  `xsave.cpp`, `selftest.cpp`, `xpad.cpp` and the generated `stubs_typed.cpp` are compiled with the SDK's own CL
  against its headers, so the structures the game reads are the SDK's.
- `scripts/mkstubs3.py` writes a stand-in for every Xbox function the layer doesn't define. Each one is written with
  the prototype from the SDK's headers, so it clears every output.
- `lld-link` links the program at image base `0x10000`, fixed and large-address-aware, with `reserve.o` first so that
  `0x11000-0x586000`, where the game's image sits on the console, stays free.
- One program runs both game versions. The layer is compiled once and decides the version when it starts (`h2n_v15`;
  an address that differs is written `H2N_VA(1.0's, 1.5's)`). The game's objects are linked twice with it, against
  1.0's symbols and tables and against 1.5's, to the same layout. `scripts/v15list.py` then writes every place where
  the 1.5 link differs into the 1.0 exe's `.h2nv15` section, and `main.c` applies that list at start when the game
  version is 1.5. Both versions' tables and adapters are in the exe (1.5's as `_h2n15_*`).

## Game version 1.5

1.5 runs the way 1.0 does: functions the title update left unchanged run as the compiled source, changed ones run as
the 1.5 XBE's own code, the data sits at 1.5's addresses. The tables for it are made from the 1.0 workspace and your
1.5 XBE by `scripts15/` (in `/home/claude/v15`, output in `/home/claude/native15`):

    python3 emuinit15.py      # -> native15/init_delta.json (what 1.5's start-up initialisers leave in memory; not published)
    ./chain15.sh              # map15, reach15, boundary15, conv15, adapt15, vt15, syms15, tables15, gen_code15, back15, ...

After a change to one source of the game: `syms15.py`, `tables15.py` and `gen_code15.py` again, then `build3.sh`.
DEVLOG.txt describes each step, what it found and the cases settled by hand. `python3 tools15/rawaddr.py`, run in
`layer3/`, lists addresses in the layer that are not inside `H2N_VA` - run it after changing the layer.

## The other programs

- H2Classic Settings, 64-bit:
  `python3 -m ziglang cc -target x86_64-windows-gnu -O2 -Wall -Wl,--subsystem,windows -o "H2Classic Settings.exe"
  launcher/h2c_settings.c res/h2classic_settings.rc -luser32 -lgdi32 -lcomdlg32 -ladvapi32`
- The DLSS helper: `dlss/build_helper.sh`. It needs the DLSS SDK above. Never put `nvngx_dlss.dll` in a package.
- Icons: `res/make_icons.py`.

## Testing under Wine

Unpack your game into `native/run/real` (the XBE and `maps`), then run:

- `tools/runreal.sh [seconds] keep`: a run on those files, then how it ended and, if it stopped, where.
  Pass `keep`, or the work folder's profiles are emptied first.
- `H2N_SELFTEST=1`: the layer's self-test, 32 checks, in an empty folder.
- `H2N_STACK=poison`: dead stack filled with `0x0BADBAD1`, so that an uninitialised local stops the run.
- `tools/live_start.sh`, `live_key.sh` and `live_stop.sh`: a run under Xvfb that takes presses from a file and saves
  frames.
- `tools/closer/closer.c`: closes the game's window so that it writes its closing report (`live_stop.sh` runs it from
  `/home/claude/d3dtest/closer.exe`; build it with `zig cc -target x86-windows-gnu`).
- `tools/test_xinput/`: a stand-in `xinput1_4.dll` with one controller that writes down the vibration it is sent.
  Build it with `zig cc -shared` and run with `WINEDLLOVERRIDES=xinput1_4=n,b`.

When a run stops, it writes `h2classic_stop.bin`. To read every thread's registers and call chain:
`python3 scripts/readdump.py h2classic_stop.bin out/halo2_native.exe out/halo2_native.map`. The exe and map must be
from the same build.
