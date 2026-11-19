# Halo 2 Classic - A Halo 2 Recompilation

<img width="3429" height="958" alt="image" src="https://github.com/user-attachments/assets/a2ddd59c-7e97-41de-a40f-84eefca7fbae" />


H2Classic runs **Halo 2 for the original Xbox** as a native Windows program. It is built from the source of
[halo2-decompiled](https://github.com/kirklandsig/halo2-decompiled), the community decompilation of the game's
executable, together with a Windows layer that does the console's work: graphics in Direct3D9, a software audio 
processor, controllers through XInput, and networking with Xemu.

H2Classic is not an emulator. The game's code is compiled for the PC. Where the decompilation doesn't have a function
yet, or only has an unproven draft of it, the program runs that function from **your own copy** of the game: at start
it reads your unmodified v1.0 `default.xbe`, puts its data and that code at the console's addresses, and runs it.
Nothing of the game, the XBE, the console or Microsoft is in this repository or in the program.
  
This is a test project, not a finished port. Builds are made and checked one change at a time; see
[docs/CHANGELOG.txt](docs/CHANGELOG.txt).

## What you need to play

- Windows 10 or 11, 64-bit.
- Your own Halo 2 game for the original Xbox: the unmodified **v1.0** `default.xbe` (4,861,952 bytes, SHA-256
  `03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`) and its `maps` folder, or a disc image (.iso) of
  the game to place in the `Roms\Halo 2` folder. The program checks the XBE and stops if it is any other version or a modified one.
- Your own copy of the Halo 2 Multiplayer Map Pack to place in the `Roms\DLC` folder. (Yes, _that_ version of it works too) .iso or
  extracted game files work.
- Optional: Generate `h2classic\keys.bin`, which makes saved games and profiles carry the same signatures as on a console, so
  they work with an Xbox or xemu. H2Classic Settings has a button that makes it from **your own** console's BIOS and
  MCPX boot ROM dumps (`launcher/make_keys.py` does the same). No key is included anywhere.
- Optional, for DLSS: NVIDIA's `nvngx_dlss.dll` next to `H2Classic_DLSS.exe` (it is NVIDIA's file and isn't included).

Start `H2Classic Settings.exe`, choose the settings, and press Start Game.

## What it does
  
<img width="3435" height="962" alt="image" src="https://github.com/user-attachments/assets/2f97a141-1a3d-471d-8b13-d0a10127f50a" />
  
- The game's own code, compiled for the PC from the decompilation, with exact game behaviour as the goal: physics,
  AI, timing and saves are the console's.
- Widescreen at 16:9, 21:9 and 32:9, with the HUD and menus placed for it; render scale and window modes.
- **Unlock Performance**: frame rates above 30. The game still runs at its own 30 ticks a second, and the frames in
  between are drawn at the in-between moment. It includes fixes ported from the Spooky Ghost mod (Increased LODs,
  Draw Distance, the zoom hold) and an animation fix for the rocket launcher.
- **Support for BOTH v1.0 and v1.5** via a selection in the Settings launcher.
- Anti-aliasing: SMAA, FXAA, TAA, and DLSS (DLAA) through a small 64-bit helper program.
- Split screen with up to four controllers, controller vibration, and full keyboard + mouse support with mappable keys.
- Software sound engine recreated to produce audio 1:1 to Xbox's original.
- System link with consoles and xemu, using the XBE's own network library (UDP tunnel in Xemu required).
- EXIT added to the main menu, so the game can be closed from full screen.

Xbox Live/Insignia is not available.

## Building

See [docs/BUILD.md](docs/BUILD.md). In short: Linux with 32-bit Wine, the Xbox SDK the game was built with (which
you must supply yourself), your own `default.xbe`, the halo2-decompiled source with
[decomp_patches/h2classic_decomp.patch](decomp_patches/h2classic_decomp.patch) applied, zig and LLVM. The developer
notes for every build are in [docs/DEVLOG.txt](docs/DEVLOG.txt).

## Repository layout

| Folder | What is in it |
| --- | --- |
| `layer2/` | The Windows layer: start-up and loading (`main.c`, `retail.c`), the renderer (`gfx9.c`, `xd3d.cpp`, `gfx_aa.c`), sound (`apu.c`, `apu_dsp.c`, `xdsound.cpp`), controllers (`input.c`, `xpad.cpp`), saves (`xsave.cpp`, `xsign.c`), files (`xapi.c`, `xiso.c`), the network (`xnet.c`, `xnetx.c`), the kernel's services (`xkernel.c`, `xcrypt.c`), frame timing and in-between frames (`timing.c`, `interp.c`) |
| `decomp_patches/` | The changes H2Classic makes to the decompilation's source: the calls into the layer, marked `h2native:` |
| `generated/` | Tables made by the analysis scripts: which retail address each data symbol and vtable stands for, which functions run from the XBE, and the code pointers in the game's data. Addresses and names only: no bytes of the XBE |
| `scripts/` | The analysis and build scripts (Python): compiling the decompilation for the PC, rewriting the objects, generating the tables, and reading crash dumps |
| `tools/` | The build and test scripts (`build2.sh`, `recompile.sh`, the Wine test-run scripts) and the SDK compiler wrappers |
| `launcher/` | H2Classic Settings (`h2c_settings.c`), the test launcher (`.cmd` + `.ps1`) and `make_keys.py` |
| `dlss/` | The 64-bit DLSS helper (`H2Classic_DLSS.exe`) and its build script |
| `res/` | Icons and version resources |
| `docs/` | Build instructions, the change log (each build's notes) and the developer log |

## Credits

- **[halo2-decompiled](https://github.com/kirklandsig/halo2-decompiled)** by kirklandsig and its contributors: the
  game's source, which this builds on.
- **Spooky Ghost** (Fortune090), the Halo 2 Xbox mod. Several of the high-frame-rate fixes are ported from it.
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and
  Diego Gutierrez; [Playdead's temporal reprojection](https://github.com/playdeadgames/temporal); NVIDIA's
  [DLSS](https://github.com/NVIDIA/DLSS). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- [xemu](https://xemu.app) served as a reference for how the console's hardware behaves.

## Legal

H2Classic is an independent, non-commercial project. It is not affiliated with, endorsed by, sponsored by or approved
by Microsoft, Bungie, 343 Industries or any other rights holder. Halo, Halo 2, Xbox and related names are trademarks
of their owners and are used here only to name the software this works with.

This repository does not contain, and the program does not include, any of the following:

- the game: its executable, maps, textures, sound, movies or other data;
- the Xbox SDK or any other Microsoft material;
- console keys or BIOS images.

You need your own copy of the game, from a disc you are legally entitled to use.

## License

H2Classic is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version
(GPL-3.0-or-later; see [LICENSE](LICENSE)). The program is built from halo2-decompiled's source, which is under the
same license.

### Additional permission for the Xbox Development Kit

H2Classic is built with the compiler and headers of the Microsoft Xbox Development Kit and runs with the libraries in
the user's own game executable. As an additional permission under section 7 of the GPL, the same as halo2-decompiled's:

If you modify this program, or any covered work, by linking or combining it with the libraries of the Microsoft Xbox
Development Kit (or modified versions of those libraries), containing parts covered by the terms of their licenses,
the licensors of this program grant you additional permission to convey the resulting work.

This permission covers only those libraries. The project's own code stays under the GPL.

### Exceptions

- `dlss/` (the separate DLSS helper program) is under the MIT license ([dlss/LICENSE](dlss/LICENSE)), because the
  helper links NVIDIA's closed NGX library, and GPL code can't be linked with it. That library and NVIDIA's DLSS SDK
  come under the [NVIDIA RTX SDKs license](dlss/NVIDIA_RTX_SDKs_LICENSE.txt). This software contains source code
  provided by NVIDIA Corporation.
- `layer2/smaa/` is SMAA's own code under its license ([layer2/smaa/LICENSE.txt](layer2/smaa/LICENSE.txt)).
