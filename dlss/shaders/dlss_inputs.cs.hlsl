// H2Classic DLSS helper: the game's motion and depth come as R16G16B16A16_FLOAT textures (the formats Direct3D 9Ex can
// share with Direct3D 11); DLSS takes motion vectors as R16G16_FLOAT and the depth with one channel (R32_FLOAT).
// motion.xy = this frame's position minus the last frame's, in the view's texture coordinates (DLSS's scale turns it
// into pixels towards the last frame); depth.x = the depth buffer's value (distance / far, 0 at the eye).
Texture2D<float4> motion_in : register(t0);
Texture2D<float4> depth_in : register(t1);
RWTexture2D<float2> motion_out : register(u0);
RWTexture2D<float> depth_out : register(u1);
cbuffer view : register(b0) { uint2 size; uint2 unused; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= size.x || id.y >= size.y) return;
    motion_out[id.xy] = motion_in.Load(int3(id.xy, 0)).xy;
    depth_out[id.xy] = depth_in.Load(int3(id.xy, 0)).x;
}
