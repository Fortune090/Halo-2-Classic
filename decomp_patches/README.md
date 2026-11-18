# Changes to the decompilation's source

`h2classic_decomp.patch` is everything H2Classic changes in [halo2-decompiled](https://github.com/kirklandsig/halo2-decompiled):
35 files. It applies to the main branch as it was on 15 November 2026 (its README: "matched 10630 of 11326 game
functions"):

    cd halo2-decompiled-main
    patch -p1 < h2classic_decomp.patch

Every change is marked `h2native:` (the project's earlier name) and is one of these kinds:

- **Calls into the Windows layer** at the places where the PC has to do something the console did differently, or
  where an H2Classic option acts: the in-between frames of Unlock Performance (`h2n_ip_*`), widescreen placement of the
  HUD, menus and text (`h2n_hud_*`, `h2n_ui_*`, `h2n_menu_*`, `h2n_ws_factor`), Spooky Ghost's Increased LODs and Draw
  Distance (`h2n_lods`, `h2n_draw_distance`), the zoom hold, the intro, frame timing (`h2n_perf`), and the rocket
  launcher's animation fix (`h2n_animation_resource`).
- **The main menu's sixth item, EXIT**, which closes the program (`main_menu.cpp`, `user_interface_lists.h`). This is
  the one deliberate change to what the game itself does.
- **Drafts switched on for the native build**: functions that the matching build leaves as `@stub` (because their
  draft is not byte-exact yet) but that the native build needs, such as the renderer's `0x2b5d0`, `0x14b60` and
  `0x3bd00`.

The functions that call the layer are compiled into H2Classic only. In the decompilation's own build, which is checked
byte for byte against the retail executable, these changes would show up as mismatches, so the patch is kept here
rather than proposed upstream.
