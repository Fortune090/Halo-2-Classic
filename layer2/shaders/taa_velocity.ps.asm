ps_3_0
; h2native: the motion of each pixel since the last frame, from the depth buffer and the game's own camera (H2N_AA=taa).
; This is Playdead's velocity prepass (github.com/playdeadgames/temporal, MIT licence; VelocityBuffer.shader,
; blit_frag_prepass) with the game's camera in place of Unity's: the pixel's view ray (through the jittered pixel, so
; the jitter cancels), the depth along it, the world position, that position in the previous frame's view, and
; ss_vel = this frame's texture coordinate - the previous one. One difference: the previous position is projected with
; the previous frame's projection (Playdead uses the current one), so a change of the field of view (zoom) moves too.
; The game's projection (function_2fb70 / function_2fd90, the record at 0x4b9e14): with x, y along the camera's right
; and up and d along its forward, ndc = (scale_x * x / d + offset_x, scale_y * y / d + offset_y); the depth buffer keeps
; d / g_48565c (function_12d50 scales the view transform by 16777215 / g_48565c and the layer stores w / 16777215).
; The console's pixel centres (c11 of function_12fa0 adds half a pixel): ndc.x = 2u - 1 - 0.5 / half_width,
; ndc.y = 1 - 2v + 0.5 / half_height, in the console's pixels.
; c0 = the view's texture coordinates -> the depth buffer's (scale.xy, offset.zw)
; c1 = (2, -2, -1 - 0.5 / half_width, 1 + 0.5 / half_height): ndc = uv * c1.xy + c1.zw
; c2 = the jitter, in this view's texture coordinates
; c3 = (1 / scale_x, 1 / scale_y, offset_x, offset_y) of this frame
; c4..c7 = this frame's camera: right, up, forward, position (.w: depth buffer value -> distance, g_48565c)
; c8..c11 = the previous frame's camera: right, up, forward, position
; c12 = (scale_x, scale_y, offset_x, offset_y) of the previous frame
; c13 = (0.5, -0.5, (1 + 0.5 / half_width) / 2, (1 + 0.5 / half_height) / 2): uv = ndc * c13.xy + c13.zw
; c15.x = 1 when s1 holds the depth from before the first-person draws (taa_depth.ps.asm), 0 when there is none
; s0 = the depth buffer (INTZ), point; s1 = that copy, point
dcl_texcoord0 v0.xy
dcl_2d s0
dcl_2d s1
def c14, 0, 1, 0, 0
mov r0, c14.x
mov r0.xy, v0
mad r1.xy, r0, c0, c0.zwzw
mov r1.zw, c14.x
texld r2, r1, s0                        ; the depth now
texld r3, r0, s1                        ; the depth before the first-person draws
; the visible point's place without the jitter, and its view ray (x / d, y / d)
add r4.xy, r0, c2
mad r5.xy, r4, c1, c1.zwzw
add r6.xy, r5, -c3.zwzw
mul r6.xy, r6, c3
; float3 vs_pos = float3(vs_ray, 1.0) * vs_dist;  float4 ws_pos = mul(cameraToWorld, float4(vs_pos, 1.0))
mul r7.x, r2.x, c7.w
mov r8.xyz, c6
mad r8.xyz, r6.x, c4, r8
mad r8.xyz, r6.y, c5, r8
mul r8.xyz, r8, r7.x
add r8.xyz, r8, c7
; float4 rp_cs_pos = mul(_PrevVP, ws_pos);  float2 rp_ss_ndc = rp_cs_pos.xy / rp_cs_pos.w;  rp_ss_txc = ...
add r9.xyz, r8, -c11
dp3 r10.x, r9, c8
dp3 r10.y, r9, c9
dp3 r10.z, r9, c10
rcp r10.z, r10.z
mul r10.xy, r10, r10.z
mad r10.xy, r10, c12, c12.zwzw
mad r10.xy, r10, c13, c13.zwzw
; float2 ss_vel = IN.ss_txc - rp_ss_txc   (both without the jitter)
add r11.xy, r4, -r10
; the first-person arms and weapon move with the camera: no motion there
add r12.x, r2.x, -r3.x
mul r12.x, r12.x_abs, c15.x
cmp r11.xy, -r12.x, r11, c14.x
mov r11.zw, c14.x
mov oC0, r11
