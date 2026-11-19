ps_3_0
; h2native: SMAA's neighborhood blending (SMAA.hlsl, SMAANeighborhoodBlendingPS without reprojection, as the DX9 demo's
; DX9_SMAANeighborhoodBlendingPS calls it), written out by hand from the HLSL.
; v0 = texcoord, v1 = offset; s0 = colorTex (the demo's colorTex: sRGB read, linear), s1 = blendTex (linear)
; c0 = SMAA_RT_METRICS
dcl_texcoord0 v0.xy
dcl_texcoord1 v1
dcl_2d s0
dcl_2d s1
def c1, 0, 1, 0.00001, 0
; a.x = SMAASample(blendTex, offset.xy).a; // Right
mov r0, c1.x
mov r0.xy, v1
texld r1, r0, s1
mov r2.x, r1.w
; a.y = SMAASample(blendTex, offset.zw).g; // Top
mov r0.xy, v1.zwzw
texld r1, r0, s1
mov r2.y, r1.y
; a.wz = SMAASample(blendTex, texcoord).xz; // Bottom / Left
mov r0.xy, v0
texld r1, r0, s1
mov r2.w, r1.x
mov r2.z, r1.z
; if (dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
dp4 r3.x, r2, c1.y
if_lt r3.x, c1.z
  ; float4 color = SMAASampleLevelZero(colorTex, texcoord); return color;
  texldl r4, r0, s0
else
  ; bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)
  max r5.x, r2.x, r2.z
  max r5.y, r2.y, r2.w
  add r5.z, r5.x, -r5.y
  ; float4 blendingOffset = float4(0.0, a.y, 0.0, a.w); SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0))
  mov r8, c1.x
  mov r8.y, r2.y
  mov r8.w, r2.w
  mov r9, c1.x
  mov r9.x, r2.x
  mov r9.z, r2.z
  cmp r6, -r5.z, r8, r9
  ; float2 blendingWeight = a.yw; SMAAMovc(bool2(h, h), blendingWeight, a.xz)
  mov r10.xy, r2.yw
  mov r11.xy, r2.xz
  cmp r7.xy, -r5.z, r10, r11
  ; blendingWeight /= dot(blendingWeight, float2(1.0, 1.0))
  add r7.z, r7.x, r7.y
  rcp r7.z, r7.z
  mul r7.xy, r7, r7.z
  ; float4 blendingCoord = mad(blendingOffset, float4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy), texcoord.xyxy)
  mov r12.xy, c0.xy
  mov r12.zw, -c0.xyxy
  mov r13, v0.xyxy
  mad r12, r6, r12, r13
  ; float4 color = blendingWeight.x * SMAASampleLevelZero(colorTex, blendingCoord.xy)
  mov r14, c1.x
  mov r14.xy, r12
  texldl r15, r14, s0
  ; color += blendingWeight.y * SMAASampleLevelZero(colorTex, blendingCoord.zw)
  mov r14.xy, r12.zwzw
  texldl r16, r14, s0
  mul r4, r15, r7.x
  mad r4, r16, r7.y, r4
endif
mov oC0, r4
