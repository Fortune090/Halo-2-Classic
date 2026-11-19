#!/bin/sh
# the anti-aliasing runs at the main menu, one after another
W=/home/claude/work_scripts/aa_menu_run.sh
H2N_AA=taa H2N_SKIPINTRO=1 H2N_SECONDS=140 $W taa
H2N_AA=dlss H2N_SKIPINTRO=1 H2N_SECONDS=140 $W dlss
H2N_AA=fxaa H2N_SKIPINTRO=1 H2N_SECONDS=140 H2N_GFXCACHE=2 $W fxaa_cache
H2N_AA=smaa H2N_SKIPINTRO=1 H2N_SECONDS=140 H2N_GFXCACHE=2 $W smaa_cache
