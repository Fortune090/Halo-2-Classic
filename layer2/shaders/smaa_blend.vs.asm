vs_3_0
; h2native: SMAA's blending weight calculation vertex shader (SMAA.hlsl, SMAABlendingWeightCalculationVS, called by the
; DX9 demo's DX9_SMAABlendingWeightCalculationVS), written out by hand:
;   pixcoord = texcoord * SMAA_RT_METRICS.zw
;   offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-0.25, -0.125,  1.25, -0.125), texcoord.xyxy)
;   offset[1] = mad(SMAA_RT_METRICS.xyxy, float4(-0.125, -0.25, -0.125,  1.25), texcoord.xyxy)
;   offset[2] = mad(SMAA_RT_METRICS.xxyy, float4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS), float4(offset[0].xz, offset[1].yw))
; c0 = SMAA_RT_METRICS; STEPS2 = 2 * SMAA_MAX_SEARCH_STEPS of the preset (given when assembling)
dcl_position v0
dcl_texcoord0 v1
dcl_position o0
dcl_texcoord0 o1
dcl_texcoord1 o2
dcl_texcoord2 o3
dcl_texcoord3 o4
dcl_texcoord4 o5
def c1, -0.25, -0.125, 1.25, -0.125
def c2, -0.125, -0.25, -0.125, 1.25
def c3, -STEPS2, STEPS2, -STEPS2, STEPS2
def c4, 0, 0, 0, 0
mov o0, v0
mov o1, v1
; pixcoord (TEXCOORD1)
mul r0.xy, v1, c0.zwzw
mov r0.zw, c4.x
mov o2, r0
; offset[0] (TEXCOORD2)
mov r1, c1
mad r1, c0.xyxy, r1, v1.xyxy
mov o3, r1
; offset[1] (TEXCOORD3)
mov r2, c2
mad r2, c0.xyxy, r2, v1.xyxy
mov o4, r2
; offset[2] (TEXCOORD4)
mov r3.xy, r1.xz
mov r3.zw, r2.xxyw
mov r4, c3
mad o5, c0.xxyy, r4, r3
