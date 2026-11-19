ps_3_0
; h2native: SMAA's blending weight calculation (SMAA.hlsl, SMAABlendingWeightCalculationPS with subsampleIndices = 0, as
; the DX9 demo's DX9_SMAABlendingWeightCalculationPS calls it), written out by hand from the HLSL, function by function:
; SMAACalculateDiagWeights with SMAASearchDiag1/2, SMAADecodeDiagBilinearAccess and SMAAAreaDiag; SMAASearchXLeft/XRight/
; YUp/YDown with SMAASearchLength; SMAAArea; SMAADetectHorizontal/VerticalCornerPattern. Each HLSL line is quoted above
; the instructions it became. The while loops are rep loops with a break for each part of their condition; the counts
; (REP_ORTHO, REP_DIAG) only have to be larger than the conditions ever allow (the preset's search steps + 2).
; round(x) is floor(x + 0.5) and sqrt(x) is 1/rsq(x), as Microsoft's compiler writes them for shader model 3.
; Preset values given when assembling: DIAGMAX = SMAA_MAX_SEARCH_STEPS_DIAG - 1, CORNERW = 1 - SMAA_CORNER_ROUNDING / 100.
; (s10g) SMAA_PRESET_LOW and _MEDIUM define SMAA_DISABLE_DIAG_DETECTION and SMAA_DISABLE_CORNER_DETECTION. Here: DIAGMAX -1,
; so each diagonal search stops before its first step (coord.z starts at -1) and d.x + d.y never passes 2 - the weights stay
; 0 and the horizontal and vertical search runs, as with the diagonals left out; and CORNERW 0, so the corner factor is 1
; and the weights are left as they are, as with the corner detection left out.
; v0 = texcoord, v1 = pixcoord, v2..v4 = offset[0..2]
; s0 = edgesTex (linear, clamped), s1 = areaTex (D3DFMT_A8L8, linear: SMAA_AREATEX_SELECT is .ra), s2 = searchTex (L8, point)
; c0 = SMAA_RT_METRICS (1/w, 1/h, w, h), c1 = (2/w, 2/h, 0.25/w, 0.25/h): set by gfx_aa.c (the reference folds these in)
dcl_texcoord0 v0.xy
dcl_texcoord1 v1.xy
dcl_texcoord2 v2
dcl_texcoord3 v3
dcl_texcoord4 v4
dcl_2d s0
dcl_2d s1
dcl_2d s2
def c2, 0, 1, 0.5, 2
def c3, 0.25, -0.25, 0.9, 0.8281
def c4, DIAGMAX, -1, 1, 0
def c5, 5, -3.75, 20, 16                ; 5.0 and -5.0 * 0.75 (diagonal decode), SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE
def c6, 1.0/160, 1.0/560, 0.5/160, 0.5/560    ; SMAA_AREATEX_PIXEL_SIZE and half of it
def c7, 0.5, -2, 0.0078125, 2.03125     ; SMAASearchLength(e, 0.0): scale, bias (worked out as the function does)
def c8, 0.5, -2, 0.5234375, 2.03125     ; SMAASearchLength(e, 0.5): scale, bias
def c9, -255.0/127, 3.25, 4, CORNERW    ; -(255.0 / 127.0), 3.25, 4.0 (SMAAArea), 1.0 - SMAA_CORNER_ROUNDING_NORM (0: corner detection off)
defi i0, REP_ORTHO, 0, 0, 0
defi i1, REP_DIAG, 0, 0, 0

; float4 weights = float4(0.0, 0.0, 0.0, 0.0)
mov r31, c2.x
; float2 e = SMAASample(edgesTex, texcoord).rg
mov r0, c2.x
mov r0.xy, v0
texld r1, r0, s0
mov r9, c2.x

; if (e.g > 0.0) { // Edge at north
if_gt r1.y, c2.x

  ; ===== weights.rg = SMAACalculateDiagWeights(edgesTex, areaTex, texcoord, e, subsampleIndices)
  ; float2 weights = float2(0.0, 0.0)
  mov r2, c2.x
  ; if (e.r > 0.0) {
  if_gt r1.x, c2.x
    ; d.xz = SMAASearchDiag1(edgesTex, texcoord, float2(-1.0, 1.0), end)
    ;   float4 coord = float4(texcoord, -1.0, 1.0)
    mov r6.xy, v0
    mov r6.zw, c4.xxyz
    ;   while (coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
    rep i1
      break_ge r6.z, c4.x
      break_le r6.w, c3.z
      ;   coord.xyz = mad(t, float3(dir, 1.0), coord.xyz)     (t = float3(SMAA_RT_METRICS.xy, 1.0), dir = (-1, 1))
      add r6.x, r6.x, -c0.x
      add r6.y, r6.y, c0.y
      add r6.z, r6.z, c2.y
      ;   e = SMAASampleLevelZero(edgesTex, coord.xy).rg
      mov r9.xy, r6
      texldl r7, r9, s0
      ;   coord.w = dot(e, float2(0.5, 0.5))
      add r6.w, r7.x, r7.y
      mul r6.w, r6.w, c2.z
    endrep
    ;   return coord.zw
    mov r4.xz, r6.zzww
    ; d.x += float(end.y > 0.9)
    add r8.x, r7.y, -c3.z
    cmp r8.x, -r8.x, c2.x, c2.y
    add r4.x, r4.x, r8.x
  ; } else d.xz = float2(0.0, 0.0)
  else
    mov r4.xz, c2.x
  endif

  ; d.yw = SMAASearchDiag1(edgesTex, texcoord, float2(1.0, -1.0), end)
  mov r6.xy, v0
  mov r6.zw, c4.xxyz
  rep i1
    break_ge r6.z, c4.x
    break_le r6.w, c3.z
    add r6.x, r6.x, c0.x
    add r6.y, r6.y, -c0.y
    add r6.z, r6.z, c2.y
    mov r9.xy, r6
    texldl r7, r9, s0
    add r6.w, r7.x, r7.y
    mul r6.w, r6.w, c2.z
  endrep
  mov r4.yw, r6.zzww

  ; if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
  add r8.x, r4.x, r4.y
  if_gt r8.x, c2.w
    ; float4 coords = mad(float4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), SMAA_RT_METRICS.xyxy, texcoord.xyxy)
    add r10.x, -r4.x, c3.x
    mov r10.y, r4.x
    mov r10.z, r4.y
    add r10.w, -r4.y, c3.y
    mov r11, v0.xyxy
    mad r10, r10, c0.xyxy, r11
    ; c.xy = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(-1,  0)).rg
    add r9.x, r10.x, -c0.x
    mov r9.y, r10.y
    texldl r12, r9, s0
    mov r14.xy, r12
    ; c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, int2( 1,  0)).rg
    add r9.x, r10.z, c0.x
    mov r9.y, r10.w
    texldl r13, r9, s0
    mov r14.zw, r13.xyxy
    ; c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw):  e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75); return round(e)
    mad r15.xz, r14, c5.x, c5.y
    mul r15.xz, r14, r15_abs
    mov r15.yw, r14
    add r15, r15, c2.z
    frc r16, r15
    add r15, r15, -r16
    mov r14, r15.yxwz
    ; float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw)
    mad r16.x, r14.x, c2.w, r14.y
    mad r16.y, r14.z, c2.w, r14.w
    ; SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0))
    add r17.xy, r4.zwzw, -c3.z
    cmp r16.xy, r17, c2.x, r16
    ; weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.z):
    ;   float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist)
    mad r18.xy, r16, c5.z, r4
    ;   texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE)
    mad r18.xy, r18, c6, c6.zwzw
    ;   texcoord.x += 0.5   (texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * 0: nothing)
    add r18.x, r18.x, c2.z
    ;   return SMAA_AREATEX_SELECT(SMAASampleLevelZero(areaTex, texcoord))     (.ra)
    mov r9.xy, r18
    texldl r19, r9, s1
    add r2.x, r2.x, r19.x
    add r2.y, r2.y, r19.w
  endif

  ; d.xz = SMAASearchDiag2(edgesTex, texcoord, float2(-1.0, -1.0), end)
  ;   float4 coord = float4(texcoord, -1.0, 1.0); coord.x += 0.25 * SMAA_RT_METRICS.x
  mov r6.xy, v0
  add r6.x, r6.x, c1.z
  mov r6.zw, c4.xxyz
  rep i1
    break_ge r6.z, c4.x
    break_le r6.w, c3.z
    add r6.x, r6.x, -c0.x
    add r6.y, r6.y, -c0.y
    add r6.z, r6.z, c2.y
    ;   e = SMAASampleLevelZero(edgesTex, coord.xy).rg; e = SMAADecodeDiagBilinearAccess(e)
    mov r9.xy, r6
    texldl r7, r9, s0
    mad r22.x, r7.x, c5.x, c5.y
    mul r7.x, r7.x, r22.x_abs
    add r7.xy, r7, c2.z
    frc r22.xy, r7
    add r7.xy, r7, -r22
    ;   coord.w = dot(e, float2(0.5, 0.5))
    add r6.w, r7.x, r7.y
    mul r6.w, r6.w, c2.z
  endrep
  mov r4.xz, r6.zzww

  ; if (SMAASampleLevelZeroOffset(edgesTex, texcoord, int2(1, 0)).r > 0.0) {
  add r9.x, v0.x, c0.x
  mov r9.y, v0.y
  texldl r20, r9, s0
  if_gt r20.x, c2.x
    ; d.yw = SMAASearchDiag2(edgesTex, texcoord, float2(1.0, 1.0), end)
    mov r6.xy, v0
    add r6.x, r6.x, c1.z
    mov r6.zw, c4.xxyz
    rep i1
      break_ge r6.z, c4.x
      break_le r6.w, c3.z
      add r6.x, r6.x, c0.x
      add r6.y, r6.y, c0.y
      add r6.z, r6.z, c2.y
      mov r9.xy, r6
      texldl r7, r9, s0
      mad r22.x, r7.x, c5.x, c5.y
      mul r7.x, r7.x, r22.x_abs
      add r7.xy, r7, c2.z
      frc r22.xy, r7
      add r7.xy, r7, -r22
      add r6.w, r7.x, r7.y
      mul r6.w, r6.w, c2.z
    endrep
    mov r4.yw, r6.zzww
    ; d.y += float(end.y > 0.9)
    add r8.x, r7.y, -c3.z
    cmp r8.x, -r8.x, c2.x, c2.y
    add r4.y, r4.y, r8.x
  ; } else d.yw = float2(0.0, 0.0)
  else
    mov r4.yw, c2.x
  endif

  ; if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
  add r8.x, r4.x, r4.y
  if_gt r8.x, c2.w
    ; float4 coords = mad(float4(-d.x, -d.x, d.y, d.y), SMAA_RT_METRICS.xyxy, texcoord.xyxy)
    mov r10.xy, -r4.x
    mov r10.zw, r4.y
    mov r11, v0.xyxy
    mad r10, r10, c0.xyxy, r11
    ; c.x  = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2(-1,  0)).g
    add r9.x, r10.x, -c0.x
    mov r9.y, r10.y
    texldl r12, r9, s0
    mov r14.x, r12.y
    ; c.y  = SMAASampleLevelZeroOffset(edgesTex, coords.xy, int2( 0, -1)).r
    mov r9.x, r10.x
    add r9.y, r10.y, -c0.y
    texldl r12, r9, s0
    mov r14.y, r12.x
    ; c.zw = SMAASampleLevelZeroOffset(edgesTex, coords.zw, int2( 1,  0)).gr
    add r9.x, r10.z, c0.x
    mov r9.y, r10.w
    texldl r12, r9, s0
    mov r14.z, r12.y
    mov r14.w, r12.x
    ; float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw)
    mad r16.x, r14.x, c2.w, r14.y
    mad r16.y, r14.z, c2.w, r14.w
    ; SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0))
    add r17.xy, r4.zwzw, -c3.z
    cmp r16.xy, r17, c2.x, r16
    ; weights += SMAAAreaDiag(areaTex, d.xy, cc, subsampleIndices.w).gr
    mad r18.xy, r16, c5.z, r4
    mad r18.xy, r18, c6, c6.zwzw
    add r18.x, r18.x, c2.z
    mov r9.xy, r18
    texldl r19, r9, s1
    add r2.x, r2.x, r19.w
    add r2.y, r2.y, r19.x
  endif
  ; ===== (return weights)
  mov r31.xy, r2

  ; if (weights.r == -weights.g) { // weights.r + weights.g == 0.0
  add r8.x, r31.x, r31.y
  if_eq r8.x, c2.x

    ; ----- coords.x = SMAASearchXLeft(edgesTex, searchTex, offset[0].xy, offset[2].x)
    ;   float2 e = float2(0.0, 1.0)
    mov r23.xy, c2.xy
    mov r24, c2.x
    mov r24.xy, v2
    ;   while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
    rep i0
      break_le r24.x, v4.x
      break_le r23.y, c3.w
      break_ne r23.x, c2.x
      ;   e = SMAASampleLevelZero(edgesTex, texcoord).rg
      texldl r25, r24, s0
      mov r23.xy, r25
      ;   texcoord = mad(-float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord)
      add r24.x, r24.x, -c1.x
    endrep
    ;   float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.0), 3.25)
    mad r26.xy, r23, c7, c7.zwzw
    mov r9.xy, r26
    texldl r27, r9, s2
    mad r27.x, r27.x, c9.x, c9.y
    ;   return mad(SMAA_RT_METRICS.x, offset, texcoord.x)
    mad r28.x, c0.x, r27.x, r24.x
    ; coords.y = offset[1].y
    mov r28.y, v3.y
    ; d.x = coords.x;  float e1 = SMAASampleLevelZero(edgesTex, coords.xy).r
    mov r9.xy, r28
    texldl r25, r9, s0
    mov r30.x, r25.x

    ; ----- coords.z = SMAASearchXRight(edgesTex, searchTex, offset[0].zw, offset[2].y)
    mov r23.xy, c2.xy
    mov r24, c2.x
    mov r24.xy, v2.zwzw
    ;   while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
    rep i0
      break_ge r24.x, v4.y
      break_le r23.y, c3.w
      break_ne r23.x, c2.x
      texldl r25, r24, s0
      mov r23.xy, r25
      ;   texcoord = mad(float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord)
      add r24.x, r24.x, c1.x
    endrep
    ;   float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e, 0.5), 3.25)
    mad r26.xy, r23, c8, c8.zwzw
    mov r9.xy, r26
    texldl r27, r9, s2
    mad r27.x, r27.x, c9.x, c9.y
    ;   return mad(-SMAA_RT_METRICS.x, offset, texcoord.x)
    mad r28.z, -c0.x, r27.x, r24.x
    ; d.y = coords.z
    mov r29.x, r28.x
    mov r29.y, r28.z
    ; d = abs(round(mad(SMAA_RT_METRICS.zz, d, -pixcoord.xx)))
    mad r29.xy, r29, c0.z, -v1.x
    add r29.xy, r29, c2.z
    frc r25.xy, r29
    add r29.xy, r29, -r25
    mov r29.xy, r29_abs
    ; float2 sqrt_d = sqrt(d)
    rsq r25.x, r29.x
    rsq r25.y, r29.y
    rcp r26.x, r25.x
    rcp r26.y, r25.y
    ; float e2 = SMAASampleLevelZeroOffset(edgesTex, coords.zy, int2(1, 0)).r
    add r9.x, r28.z, c0.x
    mov r9.y, r28.y
    texldl r25, r9, s0
    mov r30.y, r25.x
    ; weights.rg = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.y):
    ;   float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * float2(e1, e2)), dist)
    mul r27.xy, r30, c9.z
    add r27.xy, r27, c2.z
    frc r25.xy, r27
    add r27.xy, r27, -r25
    mad r27.xy, r27, c5.w, r26
    ;   texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE)   (+ SMAA_AREATEX_SUBTEX_SIZE * 0)
    mad r27.xy, r27, c6, c6.zwzw
    mov r9.xy, r27
    texldl r25, r9, s1
    mov r31.x, r25.x
    mov r31.y, r25.w
    ; coords.y = texcoord.y
    mov r28.y, v0.y
    ; SMAADetectHorizontalCornerPattern(edgesTex, weights.rg, coords.xyzy, d):
    ;   float2 leftRight = step(d.xy, d.yx)
    add r25.x, r29.y, -r29.x
    add r25.y, r29.x, -r29.y
    cmp r25.xy, r25, c2.y, c2.x
    ;   float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;  rounding /= leftRight.x + leftRight.y
    add r26.x, r25.x, r25.y
    rcp r26.x, r26.x
    mul r26.x, r26.x, c9.w
    mul r26.xy, r25, r26.x
    ;   float2 factor = float2(1.0, 1.0)
    mov r27.xy, c2.y
    ;   factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(0,  1)).r
    mov r9.x, r28.x
    add r9.y, r28.y, c0.y
    texldl r25, r9, s0
    mad r27.x, -r26.x, r25.x, r27.x
    ;   factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1,  1)).r
    add r9.x, r28.z, c0.x
    texldl r25, r9, s0
    mad r27.x, -r26.y, r25.x, r27.x
    ;   factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(0, -2)).r
    mov r9.x, r28.x
    add r9.y, r28.y, -c1.y
    texldl r25, r9, s0
    mad r27.y, -r26.x, r25.x, r27.y
    ;   factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(1, -2)).r
    add r9.x, r28.z, c0.x
    texldl r25, r9, s0
    mad r27.y, -r26.y, r25.x, r27.y
    ;   weights *= saturate(factor)
    mov_sat r27.xy, r27
    mul r31.xy, r31, r27

  ; } else e.r = 0.0; // Skip vertical processing.
  else
    mov r1.x, c2.x
  endif
endif

; if (e.r > 0.0) { // Edge at west
if_gt r1.x, c2.x
  ; ----- coords.y = SMAASearchYUp(edgesTex, searchTex, offset[1].xy, offset[2].z)
  ;   float2 e = float2(1.0, 0.0)
  mov r23.xy, c2.yx
  mov r24, c2.x
  mov r24.xy, v3
  ;   while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
  rep i0
    break_le r24.y, v4.z
    break_le r23.x, c3.w
    break_ne r23.y, c2.x
    texldl r25, r24, s0
    mov r23.xy, r25
    ;   texcoord = mad(-float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord)
    add r24.y, r24.y, -c1.y
  endrep
  ;   float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.0), 3.25)
  mad r26.xy, r23.yx, c7, c7.zwzw
  mov r9.xy, r26
  texldl r27, r9, s2
  mad r27.x, r27.x, c9.x, c9.y
  ;   return mad(SMAA_RT_METRICS.y, offset, texcoord.y)
  mad r28.y, c0.y, r27.x, r24.y
  ; coords.x = offset[0].x
  mov r28.x, v2.x
  ; d.x = coords.y;  float e1 = SMAASampleLevelZero(edgesTex, coords.xy).g
  mov r9.xy, r28
  texldl r25, r9, s0
  mov r30.x, r25.y

  ; ----- coords.z = SMAASearchYDown(edgesTex, searchTex, offset[1].zw, offset[2].w)
  mov r23.xy, c2.yx
  mov r24, c2.x
  mov r24.xy, v3.zwzw
  ;   while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
  rep i0
    break_ge r24.y, v4.w
    break_le r23.x, c3.w
    break_ne r23.y, c2.x
    texldl r25, r24, s0
    mov r23.xy, r25
    ;   texcoord = mad(float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord)
    add r24.y, r24.y, c1.y
  endrep
  ;   float offset = mad(-(255.0 / 127.0), SMAASearchLength(searchTex, e.gr, 0.5), 3.25)
  mad r26.xy, r23.yx, c8, c8.zwzw
  mov r9.xy, r26
  texldl r27, r9, s2
  mad r27.x, r27.x, c9.x, c9.y
  ;   return mad(-SMAA_RT_METRICS.y, offset, texcoord.y)
  mad r28.z, -c0.y, r27.x, r24.y
  ; d.y = coords.z
  mov r29.x, r28.y
  mov r29.y, r28.z
  ; d = abs(round(mad(SMAA_RT_METRICS.ww, d, -pixcoord.yy)))
  mad r29.xy, r29, c0.w, -v1.y
  add r29.xy, r29, c2.z
  frc r25.xy, r29
  add r29.xy, r29, -r25
  mov r29.xy, r29_abs
  ; float2 sqrt_d = sqrt(d)
  rsq r25.x, r29.x
  rsq r25.y, r29.y
  rcp r26.x, r25.x
  rcp r26.y, r25.y
  ; float e2 = SMAASampleLevelZeroOffset(edgesTex, coords.xz, int2(0, 1)).g
  mov r9.x, r28.x
  add r9.y, r28.z, c0.y
  texldl r25, r9, s0
  mov r30.y, r25.y
  ; weights.ba = SMAAArea(areaTex, sqrt_d, e1, e2, subsampleIndices.x)
  mul r27.xy, r30, c9.z
  add r27.xy, r27, c2.z
  frc r25.xy, r27
  add r27.xy, r27, -r25
  mad r27.xy, r27, c5.w, r26
  mad r27.xy, r27, c6, c6.zwzw
  mov r9.xy, r27
  texldl r25, r9, s1
  mov r31.z, r25.x
  mov r31.w, r25.w
  ; coords.x = texcoord.x
  mov r28.x, v0.x
  ; SMAADetectVerticalCornerPattern(edgesTex, weights.ba, coords.xyxz, d):
  ;   float2 leftRight = step(d.xy, d.yx)
  add r25.x, r29.y, -r29.x
  add r25.y, r29.x, -r29.y
  cmp r25.xy, r25, c2.y, c2.x
  ;   float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;  rounding /= leftRight.x + leftRight.y
  add r26.x, r25.x, r25.y
  rcp r26.x, r26.x
  mul r26.x, r26.x, c9.w
  mul r26.xy, r25, r26.x
  ;   float2 factor = float2(1.0, 1.0)
  mov r27.xy, c2.y
  ;   factor.x -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2( 1, 0)).g
  add r9.x, r28.x, c0.x
  mov r9.y, r28.y
  texldl r25, r9, s0
  mad r27.x, -r26.x, r25.y, r27.x
  ;   factor.x -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2( 1, 1)).g
  add r9.y, r28.z, c0.y
  texldl r25, r9, s0
  mad r27.x, -r26.y, r25.y, r27.x
  ;   factor.y -= rounding.x * SMAASampleLevelZeroOffset(edgesTex, texcoord.xy, int2(-2, 0)).g
  add r9.x, r28.x, -c1.x
  mov r9.y, r28.y
  texldl r25, r9, s0
  mad r27.y, -r26.x, r25.y, r27.y
  ;   factor.y -= rounding.y * SMAASampleLevelZeroOffset(edgesTex, texcoord.zw, int2(-2, 1)).g
  add r9.y, r28.z, c0.y
  texldl r25, r9, s0
  mad r27.y, -r26.y, r25.y, r27.y
  ;   weights *= saturate(factor)
  mov_sat r27.xy, r27
  mul r31.zw, r31, r27.xyxy
endif

; return weights
mov oC0, r31
