vs_3_0
; h2native: SMAA's edge detection vertex shader (SMAA.hlsl, SMAAEdgeDetectionVS, called by the DX9 demo's
; DX9_SMAAEdgeDetectionVS), written out by hand: position and texture coordinate passed on, and
;   offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-1.0, 0.0, 0.0, -1.0), texcoord.xyxy)
;   offset[1] = mad(SMAA_RT_METRICS.xyxy, float4( 1.0, 0.0, 0.0,  1.0), texcoord.xyxy)
;   offset[2] = mad(SMAA_RT_METRICS.xyxy, float4(-2.0, 0.0, 0.0, -2.0), texcoord.xyxy)
; c0 = SMAA_RT_METRICS (1/width, 1/height, width, height), set by gfx_aa.c
dcl_position v0
dcl_texcoord0 v1
dcl_position o0
dcl_texcoord0 o1
dcl_texcoord1 o2
dcl_texcoord2 o3
dcl_texcoord3 o4
def c1, -1, 0, 0, -1
def c2, 1, 0, 0, 1
def c3, -2, 0, 0, -2
mov o0, v0
mov o1, v1
mov r0, c1
mad o2, c0.xyxy, r0, v1.xyxy
mov r0, c2
mad o3, c0.xyxy, r0, v1.xyxy
mov r0, c3
mad o4, c0.xyxy, r0, v1.xyxy
