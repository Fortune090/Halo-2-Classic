# edit2.py: the console build's interpolation hooks carried other per-tick and per-draw work; that work stays, under the
# layer's own hooks of the same places (interp.c: h2n_ip_tick_before, h2n_ip_draw_before, h2n_fp_before / h2n_fp_after).
p='/home/claude/native/layer3/sg/spooky.c'; s=open(p).read()
def line_of(s,head):
    i=s.index(head); j=s.index('\n',i)+1; return i,j
i,j=line_of(s,'void ip_tick_before(void) {')
s=s[:i]+'''/* H2Classic: before each game tick (the console build's ip_tick_before without its interpolation snapshot, which the
 * layer has: interp.c h2n_ip_tick_before calls this) */
void sg_tick_before(void) { if (!sg_on) return; if (o_ctimer) ct_tick(); if (o_free_cam) fc_level_reset(); if (g_os_req) os_run(); if (g_os_fw_squad >= 0) guarded_call(os_follow_tick_g); if (o_inf_ammo || g_ia_n) guarded_call(ia_hold_tick); if (o_hiv || g_hiv_npb) { if (guarded_call(hiv_tick_g)) { o_hiv = 0; g_hiv_nb = g_hiv_ni = g_hiv_npb = 0; } } if (o_stored_vel) { if (guarded_call(sv_sample)) sv_reset(); } else if (g_sv_have) sv_reset(); }
'''+s[j:]
i,j=line_of(s,'void ip_draw_before(void) {')
s=s[:i]+'''/* H2Classic: before a frame is drawn (the console build's ip_draw_before without its blend: interp.c h2n_ip_draw_before calls this) */
void sg_draw_before(void) { if (!sg_on) return; if (g_ia_n) guarded_call(ia_hold_draw); g_wire_arm = 1; spooky_wire_on = (u8)(o_wire == 1 && wire_in_game()); }
'''+s[j:]
i,j=line_of(s,'void ip_draw_after(void) {'); s=s[:i]+s[j:]
i,j=line_of(s,'void ip_fp_before(void) {')
s=s[:i]+'''/* H2Classic: around the first-person model's set-up (the console build's ip_fp_before / ip_fp_after without the legs' turn,
 * which the layer has: interp.c h2n_fp_before / h2n_fp_after call these) */
void sg_fp_before(void) { if (!sg_on) return; guarded_call(fp_fov_scale); }
'''+s[j:]
i,j=line_of(s,'void ip_fp_after(void) {')
s=s[:i]+'void sg_fp_after(void) { if (!sg_on) return; guarded_call(fp_fov_models); tp_fp_after(); }\n'+s[j:]
open(p,'w').write(s); print('ok')
