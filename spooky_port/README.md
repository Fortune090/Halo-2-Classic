# The Spooky Ghost Menu in H2Classic: how layer3/sg/spooky.c is made

`layer3/sg/spooky.c` is generated from the mod's own `spooky.c` (Spooky Ghost Menu v2.0.40); it is not edited by hand.

1. `strip_ifdef.py` takes the host-test, size-test and trainer branches out (`spooky_stripped.c` is its result, with
   `sg_port.h` included in place of the console build's address headers).
2. `pipeline.sh` removes the entry points of what the port leaves out (`prune.py remove ...`), applies the hand edits in
   `edits/edit0.py` .. in order, then compiles with `-Wunused-function` and removes whatever nothing uses any more,
   until nothing is left to remove.
3. `gen_addr.py` writes `layer3/sg/sg_addr.h` from the mod's `addr_v10.h` and `addr_v15.h`: every data address for both
   game versions, picked when it is used. Code addresses are not in it: `sg_port.h`, `sg_game.c` and `sg_hooks.c` name
   each game function the mod calls or hooks by its own symbol, with the arguments the decompilation declares.
4. The hooks: `generated/sg_hooks.json` + `scripts/sg_hooks.py` re-aim the hooked calls in the compiled game objects
   (after `rewrite.py` and `scene_hook.py`); `layer3/sg/sg_sites.h` + `sg_retail.S` are the console build's own routines
   for the same calls where the XBE's own code runs.

`hooks_list.py`, `sites.py`, `ncalls.py`, `classify.py` and `hx.py` are the tools used to work that out: the mod's hook
table (`hooks.json`), where each call site is and how H2Classic runs the function it is in, how many of the game's calls
of a function the mod hooks, where each address the mod names lands here, and a hook routine's text.

A new feature of the mod is brought over by: taking its entry point out of `pipeline.sh`'s remove list (or adding a
hand edit), adding its hook to `sg_hooks.json` / `sg_hooks.c` (and `sg_sites.h` / `sg_retail.S`), and setting its switch
in `sg_port.h`. Every switch in `sg_port.h` is on from build 52 (Wireframe and Tick Rate came last: `edits/edit9.py`, the draw and fill-mode hooks, and the layer's Direct3D for the white lines and the real-edge wireframe - `layer3/gfx9.c`).
