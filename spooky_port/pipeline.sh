#!/bin/sh
# pipeline.sh: the port's spooky.c made again from the mod's own file (uploads_sg): strip the host branches, take out the
# entry points of what this build leaves out, apply the hand edits, then remove whatever nothing uses any more.
set -e
G=/home/claude/sgport; D=/home/claude/native/layer3/sg
cp $G/spooky_stripped.c $D/spooky.c
python3 -I $G/prune.py $D/spooky.c remove memset memcpy aniso_clamp cache_anim_take cache_geo_take coop_apply crash_record draw_dist_view fog_after fog_before hd_glyph_args hd_glyph_header help_box_after help_box_before hsq_anchor hsq_glyph hsq_grid hsq_quad hsq_text_before hsq_track_begin hsq_track_end hsq_trkv ip_dir_before light_begin light_end mark menu_pic_after menu_pic_before menu_pic_next menu_text_after menu_text_before model_scene_before movie_quad pass_in pass_out prof_present_after prof_present_before prof_wait_after prof_wait_before proj_clip prompt_text_after prompt_text_before rec_in rec_out rfill_rect rs_after_ui_point rs_before rs_big_swap rs_cut rs_stretch rs_ui_point rt_size rt_switch sb_panel_after sb_panel_before scr_post scr_pre se_after se_before sprite_after sprite_before swap_settle swap_tail_check tb_load title_text_after title_text_before ui_win_after ui_win_before ui_wrap_after ui_wrap_before vp_filter water_fit water_late_copy cam_range_add cam_range_reset cam_seen perf_defaults fx_view | cut -c1-100
for e in $G/edits/edit[0-9]*.py; do python3 $e; done
for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
  SGW="-Wunused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-const-variable -DSG_PRUNE_PASS" $G/cc.sh > /tmp/cc_out.txt
  n=$(grep -c "warning: unused function" /tmp/sg_err.txt || true); e=$(grep -c ' error: ' /tmp/sg_err.txt || true)
  echo "pass $i: unused $n errors $e"
  [ "$e" != 0 ] && { grep ' error: ' /tmp/sg_err.txt | head -12 | cut -c1-200; exit 1; }
  [ "$n" = 0 ] && break
  python3 -I $G/prune.py $D/spooky.c unused /tmp/sg_err.txt | cut -c1-100
done
wc -l $D/spooky.c
