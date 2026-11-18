ps_3_0
; h2native: the view's depth as it is just before the game draws the first-person arms and weapon (H2N_AA=taa).
; The game draws those with its view transform scaled by 0.15 (unknown_01cf50.cpp, function_12d50(true, ...) for entries
; with flag 0x800, between g_485a76 = 1 and 0 in function_2bcd0), so they move with the camera and the motion of the
; world does not apply to them: where the depth differs from this copy at the end of the view, the motion is set to 0.
; c0 = (scale, offset): the view's texture coordinates -> the depth buffer's; s0 = the depth buffer (INTZ), point
dcl_texcoord0 v0.xy
dcl_2d s0
def c1, 0, 0, 0, 0
mov r0, c1.x
mov r0.xy, v0
mad r0.xy, r0, c0, c0.zwzw
texld r1, r0, s0
mov r1, r1.x                            ; oC# takes no swizzle (Output Color Register, Direct3D 9)
mov oC0, r1
