ps_3_0
; h2native: edge smoothing on the finished picture (H2N_AA=fxaa), the layer's own code in the manner of FXAA:
; find where brightness changes sharply, which way the edge runs, how far it goes each way, and blend this pixel
; with its neighbour across the edge by how near the edge's end is (plus a little for one-pixel detail).
; c0 = (1/width, 1/height, -1/width, -1/height), set for each picture; s0 = the picture, filtered, clamped.
def c1, 0.299, 0.587, 0.114, 0          ; brightness from colour
def c2, 0.0625, 0.125, 0.75, 0.5        ; least contrast that is an edge, contrast as a part of the brightest, detail amount, half
def c3, 2, 0.0833333, 0.25, 3           ; two, 1/12, quarter, three
def c4, 1, 2, 3, 4                      ; how far along the edge each look goes, in pixels
def c5, 5.5, 7.5, 9.5, 11.5
def c6, 15.5, 23.5, 0, 1
dcl_texcoord0 v0.xy
dcl_2d s0
mov r0, c6.z
mov r0.xy, v0.xy                        ; this pixel (lod 0 in w)
texldl r1, r0, s0
mov r31, r1                             ; the answer when there is no edge
dp3 r2.x, r1, c1                        ; M
mov r5, r0
add r5.y, r0.y, c0.w
texldl r6, r5, s0
dp3 r2.y, r6, c1                        ; N
add r5.y, r0.y, c0.y
texldl r6, r5, s0
dp3 r2.z, r6, c1                        ; S
mov r5, r0
add r5.x, r0.x, c0.z
texldl r6, r5, s0
dp3 r2.w, r6, c1                        ; W
add r5.x, r0.x, c0.x
texldl r6, r5, s0
dp3 r3.x, r6, c1                        ; E
max r7.x, r2.x, r2.y
max r7.x, r7.x, r2.z
max r7.x, r7.x, r2.w
max r7.x, r7.x, r3.x                    ; brightest
min r7.y, r2.x, r2.y
min r7.y, r7.y, r2.z
min r7.y, r7.y, r2.w
min r7.y, r7.y, r3.x                    ; darkest
add r7.z, r7.x, -r7.y                   ; contrast
mul r7.w, r7.x, c2.y
max r7.w, r7.w, c2.x                    ; the contrast that makes an edge here
if_ge r7.z, r7.w
  mov r5, r0
  add r5.xy, r0.xy, c0.zw
  texldl r6, r5, s0
  dp3 r3.y, r6, c1                      ; NW
  add r5.x, r0.x, c0.x
  texldl r6, r5, s0
  dp3 r3.z, r6, c1                      ; NE
  add r5.y, r0.y, c0.y
  texldl r6, r5, s0
  dp3 r4.x, r6, c1                      ; SE
  add r5.x, r0.x, c0.z
  texldl r6, r5, s0
  dp3 r3.w, r6, c1                      ; SW
  ; one-pixel detail: how far this pixel is from the average around it, eased
  add r8.x, r2.y, r2.z
  add r8.x, r8.x, r2.w
  add r8.x, r8.x, r3.x
  add r8.y, r3.y, r3.z
  add r8.y, r8.y, r3.w
  add r8.y, r8.y, r4.x
  mad r8.z, r8.x, c3.x, r8.y
  mul r8.z, r8.z, c3.y
  add r8.z, r8.z, -r2.x
  rcp r8.w, r7.z
  mul_sat r8.z, r8.z_abs, r8.w
  mad r9.x, r8.z, -c3.x, c3.w
  mul r9.y, r8.z, r8.z
  mul r9.x, r9.x, r9.y
  mul r9.x, r9.x, r9.x
  mul r9.x, r9.x, c2.z                  ; the detail's share
  ; which way the edge runs: change across rows against change across columns
  mad r10.x, r2.x, -c3.x, r2.y
  add r10.x, r10.x, r2.z
  mad r10.y, r2.w, -c3.x, r3.y
  add r10.y, r10.y, r3.w
  mad r10.z, r3.x, -c3.x, r3.z
  add r10.z, r10.z, r4.x
  add r11.x, r10.y_abs, r10.z_abs
  mad r11.x, r10.x_abs, c3.x, r11.x     ; across rows (the edge runs along a row)
  mad r10.x, r2.x, -c3.x, r2.w
  add r10.x, r10.x, r3.x
  mad r10.y, r2.y, -c3.x, r3.y
  add r10.y, r10.y, r3.z
  mad r10.z, r2.z, -c3.x, r3.w
  add r10.z, r10.z, r4.x
  add r11.y, r10.y_abs, r10.z_abs
  mad r11.y, r10.x_abs, c3.x, r11.y     ; across columns
  add r11.w, r11.x, -r11.y
  cmp r11.z, r11.w, c6.w, c6.z          ; 1: the edge runs along a row
  ; which side of this pixel the edge is on
  lrp r12.x, r11.z, r2.y, r2.w          ; N or W
  lrp r12.y, r11.z, r2.z, r3.x          ; S or E
  add r12.z, r12.x, -r2.x
  add r12.w, r12.y, -r2.x
  add r13.x, r12.z_abs, -r12.w_abs
  cmp r13.y, r13.x, c6.w, c6.z          ; 1: on the N (or W) side
  max r13.z, r12.z_abs, r12.w_abs
  mul r13.z, r13.z, c3.z                ; a quarter of the step: where the edge is taken to end
  lrp r13.w, r13.y, r12.x, r12.y
  add r13.w, r13.w, r2.x
  mul r13.w, r13.w, c2.w                ; the brightness on the edge
  lrp r14.x, r11.z, c0.y, c0.x
  cmp r14.y, -r13.y, r14.x, -r14.x      ; one pixel towards the edge's side
  add r15.w, c6.w, -r11.z
  mul r15.y, r14.y, r11.z
  mul r15.x, r14.y, r15.w               ; across the edge
  mul r16.x, c0.x, r11.z
  mul r16.y, c0.y, r15.w                ; along the edge
  mov r17, r0
  mad r17.xy, r15.xy, c2.w, r0.xy       ; on the edge
  mov r18, r17
  mov r19, r17
  mov r26, c6.z                         ; brightness at each end
  mov r27, c6.z                         ; distance to each end
  mov r28, c6.w                         ; each end not found yet
  ; along the edge both ways, ten looks each, until the brightness leaves the edge's
  mad r18.xy, r16.xy, -c4.x, r17.xy
  mad r19.xy, r16.xy, c4.x, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c4.x, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c4.y, r17.xy
  mad r19.xy, r16.xy, c4.y, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c4.y, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c4.z, r17.xy
  mad r19.xy, r16.xy, c4.z, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c4.z, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c4.w, r17.xy
  mad r19.xy, r16.xy, c4.w, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c4.w, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c5.x, r17.xy
  mad r19.xy, r16.xy, c5.x, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c5.x, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c5.y, r17.xy
  mad r19.xy, r16.xy, c5.y, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c5.y, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c5.z, r17.xy
  mad r19.xy, r16.xy, c5.z, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c5.z, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c5.w, r17.xy
  mad r19.xy, r16.xy, c5.w, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c5.w, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c6.x, r17.xy
  mad r19.xy, r16.xy, c6.x, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c6.x, r27.xy
  add r29.xy, r21.xy_abs, -r13.z
  cmp r28.xy, r29.xy, c6.z, r28.xy
  mad r18.xy, r16.xy, -c6.y, r17.xy
  mad r19.xy, r16.xy, c6.y, r17.xy
  texldl r20, r18, s0
  dp3 r21.x, r20, c1
  texldl r20, r19, s0
  dp3 r21.y, r20, c1
  add r21.xy, r21.xy, -r13.w
  lrp r26.xy, r28.xy, r21.xy, r26.xy
  lrp r27.xy, r28.xy, c6.y, r27.xy
  ; how far into the edge this pixel is, from the nearer end
  min r30.x, r27.x, r27.y
  add r30.y, r27.x, r27.y
  rcp r30.z, r30.y
  mul r30.w, r30.x, r30.z
  add r30.w, -r30.w, c2.w               ; a half at the nearer end, nothing in the middle
  add r22.x, r27.x, -r27.y
  cmp r22.y, r22.x, r26.y, r26.x        ; the brightness at the nearer end
  add r22.z, r2.x, -r13.w
  mul r22.w, r22.y, r22.z
  cmp r23.x, r22.w, c6.z, r30.w         ; blend only where the edge turns towards this pixel's side
  max r23.x, r23.x, r9.x
  mov r24, r0
  mad r24.xy, r15.xy, r23.x, r0.xy
  texldl r31, r24, s0
endif
mov oC0, r31
