ps_3_0
; h2native: SMAA's luma edge detection (SMAA.hlsl, SMAALumaEdgeDetectionPS, as the DX9 demo's
; DX9_SMAALumaEdgeDetectionPS returns it: float4(edges, 0, 0)), written out by hand from the HLSL.
; THRESHOLD is SMAA_THRESHOLD of the preset (given when assembling); SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR = 2.0.
; v0 = texcoord, v1..v3 = offset[0..2]; s0 = the colour, read without sRGB (the demo's colorGammaTex), linear, clamped.
dcl_texcoord0 v0.xy
dcl_texcoord1 v1
dcl_texcoord2 v2
dcl_texcoord3 v3
dcl_2d s0
def c1, 0.2126, 0.7152, 0.0722, 0       ; float3 weights = float3(0.2126, 0.7152, 0.0722)
def c2, THRESHOLD, 2, -0.5, 0           ; threshold, local contrast adaptation factor, -0.5 (for the discard test)
def c3, 0, 1, 0, 0
; float L = dot(SMAASamplePoint(colorTex, texcoord).rgb, weights)
mov r0, c3.x
mov r0.xy, v0
texld r0, r0, s0
dp3 r0.x, r0, c1
; float Lleft = dot(SMAASamplePoint(colorTex, offset[0].xy).rgb, weights)
mov r1, c3.x
mov r1.xy, v1
texld r2, r1, s0
dp3 r2.x, r2, c1
; float Ltop = dot(SMAASamplePoint(colorTex, offset[0].zw).rgb, weights)
mov r1.xy, v1.zwzw
texld r3, r1, s0
dp3 r2.y, r3, c1
; delta.xy = abs(L - float2(Lleft, Ltop))
add r4.xy, r0.x, -r2
mov r4.xy, r4_abs
; float2 edges = step(threshold, delta.xy)
add r5.xy, r4, -c2.x
cmp r5.xy, r5, c3.y, c3.x
; if (dot(edges, float2(1.0, 1.0)) == 0.0) discard;
add r6, r5.x, r5.y
add r6, r6, c2.z
texkill r6
; float Lright = dot(SMAASamplePoint(colorTex, offset[1].xy).rgb, weights)
mov r1.xy, v2
texld r3, r1, s0
dp3 r3.x, r3, c1
; float Lbottom = dot(SMAASamplePoint(colorTex, offset[1].zw).rgb, weights)
mov r1.xy, v2.zwzw
texld r7, r1, s0
dp3 r3.y, r7, c1
; delta.zw = abs(L - float2(Lright, Lbottom))
add r4.zw, r0.x, -r3.xyxy
mov r4.zw, r4_abs
; float2 maxDelta = max(delta.xy, delta.zw)
max r8.xy, r4, r4.zwzw
; float Lleftleft = dot(SMAASamplePoint(colorTex, offset[2].xy).rgb, weights)
mov r1.xy, v3
texld r3, r1, s0
dp3 r3.x, r3, c1
; float Ltoptop = dot(SMAASamplePoint(colorTex, offset[2].zw).rgb, weights)
mov r1.xy, v3.zwzw
texld r7, r1, s0
dp3 r3.y, r7, c1
; delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop))
add r4.zw, r2.xyxy, -r3.xyxy
mov r4.zw, r4_abs
; maxDelta = max(maxDelta.xy, delta.zw)
max r8.xy, r8, r4.zwzw
; float finalDelta = max(maxDelta.x, maxDelta.y)
max r8.z, r8.x, r8.y
; edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy)
mad r9.xy, r4, c2.y, -r8.z
cmp r9.xy, r9, c3.y, c3.x
mul r5.xy, r5, r9
; return float4(edges, 0.0, 0.0)
mov r5.zw, c3.x
mov oC0, r5
