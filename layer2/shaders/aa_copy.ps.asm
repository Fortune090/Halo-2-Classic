ps_3_0
; h2native: a picture copied as it is (point sampling, texel for pixel): the anti-aliased view back into the game's
; picture (drawn with only red, green and blue written, so that the game's own alpha stays as the game left it)
dcl_texcoord0 v0.xy
dcl_2d s0
def c0, 0, 0, 0, 0
mov r0, c0.x
mov r0.xy, v0
texld r1, r0, s0
mov oC0, r1
