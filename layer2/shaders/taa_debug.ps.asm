ps_3_0
; h2native: a test view of the temporal anti-aliasing's inputs (H2N_AADEBUG), drawn in place of the result:
;   c0.x = 1: the motion (|velocity| of x in red, of y in green, times c0.z: 50 = one fiftieth of the view a frame is full)
;   c0.y = 1: how well the history lines up: |this frame (without its jitter) - the history moved by the motion| times c0.w,
;             before any clipping (what is left on still walls while turning or moving is what the motion got wrong)
; s0 = this frame's view, s1 = the history, s2 = the motion; c1 = the jitter in texture coordinates (xy)
dcl_texcoord0 v0.xy
dcl_2d s0
dcl_2d s1
dcl_2d s2
def c2, 0, 1, 0, 0
mov r0, c2.x
mov r0.xy, v0
texld r1, r0, s2
; the motion
mul r2.xy, r1_abs, c0.z
mov r2.zw, c2.x
; the difference
mov r3, c2.x
add r3.xy, r0, -c1
texld r4, r3, s0
add r3.xy, r0, -r1
texld r5, r3, s1
add r6, r4, -r5
mul r6, r6_abs, c0.w
; which
mul r7, r2, c0.x
mad r7, r6, c0.y, r7
mov r7.w, c2.y
mov oC0, r7
