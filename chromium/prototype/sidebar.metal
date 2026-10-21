#include <metal_stdlib>
using namespace metal;
struct Quad { float2 origin; float2 size; float radius; uint color; uint textured; uint pad; };
struct Out { float4 position [[position]]; float2 uv; float2 size; float radius; float4 color; uint textured [[flat]]; };
vertex Out ui_vertex(uint v [[vertex_id]], constant Quad& q [[buffer(0)]], constant float2& viewport [[buffer(1)]]) {
  const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
  float2 p = q.origin + corners[v] * q.size;
  Out o;
  o.position = float4(p.x/viewport.x*2-1,1-p.y/viewport.y*2,0,1);
  o.uv = corners[v]; o.size=q.size; o.radius=q.radius; o.textured=q.textured;
  o.color=float4(float((q.color>>16)&255),float((q.color>>8)&255),float(q.color&255),float(q.color>>24))/255;
  return o;
}
fragment float4 ui_fragment(Out in [[stage_in]], texture2d<float> text [[texture(0)]]) {
  if (in.textured) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return text.sample(s,in.uv);
  }
  float2 d=abs((in.uv-0.5)*in.size)-(in.size*0.5-in.radius);
  float dist=length(max(d,0.0))+min(max(d.x,d.y),0.0)-in.radius;
  float a=in.color.a*(1-smoothstep(-0.5,0.5,dist));
  return float4(in.color.rgb*a,a);
}
