ps_3_0
; h2native: temporal anti-aliasing (H2N_AA=taa): Playdead's TemporalReprojection.shader (github.com/playdeadgames/temporal,
; MIT licence) with the options of its own defaults (TemporalReprojection.cs) except motion blur, which is not
; anti-aliasing: MINMAX_3X3_ROUNDED, UNJITTER_COLORSAMPLES, USE_CLIPPING, USE_DILATION (find_closest_fragment_3x3),
; USE_OPTIMIZATIONS; no YCoCg; feedback 0.88 .. 0.97; Unity's Luminance() of gamma space (0.22, 0.707, 0.071); the
; noise it adds (PDsrand4 / 510). Written out by hand from the Cg, line by line (quoted above the instructions).
; v0 = ss_txc (this view's texture coordinate)
; s0 = _MainTex (this frame's view, jittered), linear; s1 = _PrevTex (the history), linear
; s2 = _VelocityBuffer (taa_velocity.ps.asm), point; s3 = _CameraDepthTexture (the depth buffer, INTZ), point
; c0 = the view's texture coordinates -> the depth buffer's (scale.xy, offset.zw)
; c1 = _MainTex_TexelSize (1/w, 1/h, w, h); c2 = the depth buffer's texel size (1/w, 1/h, 0, 0)
; c3 = _JitterUV (this frame's, the previous frame's); c4 = (_FeedbackMin, _FeedbackMax, _SinTime.x, 0)
dcl_texcoord0 v0.xy
dcl_2d s0
dcl_2d s1
dcl_2d s2
dcl_2d s3
def c5, 0.22, 0.707, 0.071, 0           ; Luminance() (unity_ColorSpaceLuminance, gamma space)
def c6, 0, 1, 0.5, 0.2
def c7, 1.0/9, 1.0/5, 0.00000001, -1    ; 1/9, 1/5, FLT_EPS, -1
def c8, 12.9898, 78.233, 0.6959174, 1.0/510
def c9, 43758.5453, 28001.8384, 50849.4141, 12996.89
def c10, 0.15915494309, 0.5, 6.28318530718, -3.14159265359   ; sin(): the angle brought into -pi..pi first
def c11, -1, 0, 1, 0
def c12, 2, -1, 0, 0
mov r0, c6.x
mov r0.xy, v0

; ----- float3 c_frag = find_closest_fragment_3x3(uv)   (ZCMP_GT(a, b) = a > b: the nearest depth)
mad r1.xy, r0, c0, c0.zwzw
mov r1.zw, c6.x
mov r3, c6.x
; float3 dtl = float3(-1, -1, depth(uv - dv - du));  float3 dmin = dtl
add r3.xy, r1, -c2
texld r4, r3, s3
mov r2.xy, c7.w
mov r2.z, r4.x
; dtc = float3( 0, -1, depth(uv - dv));  if (ZCMP_GT(dmin.z, dtc.z)) dmin = dtc
mov r3.x, r1.x
add r3.y, r1.y, -c2.y
texld r4, r3, s3
mov r5.xy, c11.yx
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dtr = float3( 1, -1, depth(uv - dv + du))
add r3.x, r1.x, c2.x
texld r4, r3, s3
mov r5.xy, c11.zx
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dml = float3(-1, 0, depth(uv - du))
add r3.x, r1.x, -c2.x
mov r3.y, r1.y
texld r4, r3, s3
mov r5.xy, c11.xy
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dmc = float3( 0, 0, depth(uv))
mov r3.x, r1.x
texld r4, r3, s3
mov r5.xy, c11.y
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dmr = float3( 1, 0, depth(uv + du))
add r3.x, r1.x, c2.x
texld r4, r3, s3
mov r5.xy, c11.zy
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dbl = float3(-1, 1, depth(uv + dv - du))
add r3.x, r1.x, -c2.x
add r3.y, r1.y, c2.y
texld r4, r3, s3
mov r5.xy, c11.xz
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dbc = float3( 0, 1, depth(uv + dv))
mov r3.x, r1.x
texld r4, r3, s3
mov r5.xy, c11.yz
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; dbr = float3( 1, 1, depth(uv + dv + du))
add r3.x, r1.x, c2.x
texld r4, r3, s3
mov r5.xy, c11.z
mov r5.z, r4.x
add r6.x, r4.x, -r2.z
cmp r2.xyz, r6.x, r2, r5
; float2 ss_vel = tex2D(_VelocityBuffer, c_frag.xy).xy     (c_frag.xy = uv + texel size * dmin.xy)
mad r7.xy, r2, c1, r0
mov r7.zw, c6.x
texld r8, r7, s2

; ----- float4 color_temporal = temporal_reprojection(IN.ss_txc, ss_vel, vs_dist)
mov r9, c6.x
; float4 texel0 = sample_color(_MainTex, ss_txc - _JitterUV.xy)
add r9.xy, r0, -c3
texld r10, r9, s0
; float4 texel1 = sample_color(_PrevTex, ss_txc - ss_vel)
add r9.xy, r0, -r8
texld r11, r9, s1
; the neighbourhood: ctl ctc ctr / cml cmc cmr / cbl cbc cbr; cmin, cmax, cavg over all nine, cmin5, cmax5, cavg5 over the cross
; ctl = uv - dv - du
add r9.xy, r0, -c1
texld r12, r9, s0
mov r13, r12
mov r14, r12
mov r15, r12
; ctc = uv - dv
mov r9.x, r0.x
add r9.y, r0.y, -c1.y
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
mov r16, r12
mov r17, r12
mov r18, r12
; ctr = uv - dv + du
add r9.x, r0.x, c1.x
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
; cml = uv - du
add r9.x, r0.x, -c1.x
mov r9.y, r0.y
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
min r16, r16, r12
max r17, r17, r12
add r18, r18, r12
; cmc = uv
mov r9.x, r0.x
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
min r16, r16, r12
max r17, r17, r12
add r18, r18, r12
; cmr = uv + du
add r9.x, r0.x, c1.x
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
min r16, r16, r12
max r17, r17, r12
add r18, r18, r12
; cbl = uv + dv - du
add r9.x, r0.x, -c1.x
add r9.y, r0.y, c1.y
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
; cbc = uv + dv
mov r9.x, r0.x
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
min r16, r16, r12
max r17, r17, r12
add r18, r18, r12
; cbr = uv + dv + du
add r9.x, r0.x, c1.x
texld r12, r9, s0
min r13, r13, r12
max r14, r14, r12
add r15, r15, r12
; float4 cavg = (...) / 9.0;  float4 cavg5 = (...) / 5.0
mul r15, r15, c7.x
mul r18, r18, c7.y
; cmin = 0.5 * (cmin + cmin5); cmax = 0.5 * (cmax + cmax5); cavg = 0.5 * (cavg + cavg5)
add r13, r13, r16
mul r13, r13, c6.z
add r14, r14, r17
mul r14, r14, c6.z
add r15, r15, r18
mul r15, r15, c6.z

; ----- texel1 = clip_aabb(cmin.xyz, cmax.xyz, clamp(cavg, cmin, cmax), texel1)   (USE_OPTIMIZATIONS)
; p = clamp(cavg, cmin, cmax)
max r19, r15, r13
min r19, r19, r14
; float3 p_clip = 0.5 * (aabb_max + aabb_min)
add r20.xyz, r14, r13
mul r20.xyz, r20, c6.z
; float3 e_clip = 0.5 * (aabb_max - aabb_min) + FLT_EPS
add r21.xyz, r14, -r13
mul r21.xyz, r21, c6.z
add r21.xyz, r21, c7.z
; float4 v_clip = q - float4(p_clip, p.w)
mov r20.w, r19.w
add r22, r11, -r20
; float3 v_unit = v_clip.xyz / e_clip
rcp r23.x, r21.x
rcp r23.y, r21.y
rcp r23.z, r21.z
mul r23.xyz, r22, r23
; float3 a_unit = abs(v_unit);  float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z))
max r24.x, r23.y_abs, r23.z_abs
max r24.x, r23.x_abs, r24.x
; if (ma_unit > 1.0) return float4(p_clip, p.w) + v_clip / ma_unit; else return q;
rcp r24.y, r24.x
mad r25, r22, r24.y, r20
add r24.z, r24.x, -c6.y
cmp r11, -r24.z, r11, r25

; ----- feedback weight from unbiased luminance diff (t.lottes)
; float lum0 = Luminance(texel0.rgb);  float lum1 = Luminance(texel1.rgb)
dp3 r26.x, r10, c5
dp3 r26.y, r11, c5
; float unbiased_diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2))
add r26.z, r26.x, -r26.y
max r26.w, r26.y, c6.w
max r26.w, r26.x, r26.w
rcp r26.w, r26.w
mul r26.z, r26.z_abs, r26.w
; float unbiased_weight = 1.0 - unbiased_diff;  float unbiased_weight_sqr = unbiased_weight * unbiased_weight
add r26.z, -r26.z, c6.y
mul r26.z, r26.z, r26.z
; float k_feedback = lerp(_FeedbackMin, _FeedbackMax, unbiased_weight_sqr)
lrp r27.x, r26.z, c4.y, c4.x
; return lerp(texel0, texel1, k_feedback)
lrp r28, r27.x, r11, r10

; ----- float4 noise4 = PDsrand4(IN.ss_txc + _SinTime.x + 0.6959174) / 510.0;  OUT.buffer = saturate(to_buffer + noise4)
mov r29.x, c4.z
add r29.x, r29.x, c8.z
add r30.xy, r0, r29.x
; frac(sin(dot(n.xy, float2(12.9898f, 78.233f))) * float4(43758.5453f, 28001.8384f, 50849.4141f, 12996.89f))
mul r30.xy, r30, c8
add r30.x, r30.x, r30.y
mad r30.x, r30.x, c10.x, c10.y
frc r30.x, r30.x
mad r30.x, r30.x, c10.z, c10.w
sincos r31.xy, r30.x
mul r31, r31.y, c9
frc r31, r31
; * 2 - 1, / 510
mad r31, r31, c12.x, c12.y
mul r31, r31, c8.w
add_sat r28, r28, r31
mov oC0, r28
