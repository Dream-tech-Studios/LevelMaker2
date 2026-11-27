// Shaders.metal : the whole 3D preview, in two functions.
//
// The Win32 build used OpenGL 1.1 flat shading, so lighting is already a
// per-face constant. Rather than carry normals to the GPU and re-derive it,
// the CPU bakes ShadeColor's result into each vertex -- which reproduces the
// fixed-function look exactly and keeps this file inside the Metal 1.0
// feature set, so it runs on everything back to the A12X iPad Pro.
//
#include <metal_stdlib>
using namespace metal;

struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 color    [[attribute(1)]];
};

struct VertexOut
{
    float4 position [[position]];
    float3 color;
};

struct Uniforms
{
    float4x4 mvp;
};

vertex VertexOut lm_vertex(VertexIn in [[stage_in]],
                           constant Uniforms &u [[buffer(1)]])
{
    VertexOut out;
    out.position = u.mvp * float4(in.position, 1.0);
    out.color = in.color;
    return out;
}

fragment float4 lm_fragment(VertexOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
