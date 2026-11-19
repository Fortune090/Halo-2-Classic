vs_3_0
; h2native: SMAA's neighborhood blending vertex shader (SMAA.hlsl, SMAANeighborhoodBlendingVS, called by the DX9 demo's
; DX9_SMAANeighborhoodBlendingVS), written out by hand:
;   offset = mad(SMAA_RT_METRICS.xyxy, float4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy)
; c0 = SMAA_RT_METRICS
dcl_position v0
dcl_texcoord0 v1
dcl_position o0
dcl_texcoord0 o1
dcl_texcoord1 o2
def c1, 1, 0, 0, 1
mov o0, v0
mov o1, v1
mov r0, c1
mad o2, c0.xyxy, r0, v1.xyxy
