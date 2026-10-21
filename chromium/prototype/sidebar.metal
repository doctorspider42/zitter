#include <metal_stdlib>
using namespace metal;
struct Quad { float2 origin; float2 size; float radius; uint color; uint textured; float stroke; };
struct Out { float4 position [[position]]; float2 uv; float2 size; float radius; float stroke; float4 color; uint textured [[flat]]; };
vertex Out ui_vertex(uint v [[vertex_id]], constant Quad& q [[buffer(0)]], constant float2& viewport [[buffer(1)]]) {
  const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
  float2 p = q.origin + corners[v] * q.size;
  Out o;
  o.position = float4(p.x/viewport.x*2-1,1-p.y/viewport.y*2,0,1);
  o.uv = corners[v]; o.size=q.size; o.radius=q.radius; o.stroke=q.stroke; o.textured=q.textured;
  o.color=float4(float((q.color>>16)&255),float((q.color>>8)&255),float(q.color&255),float(q.color>>24))/255;
  return o;
}
float segment_distance(float2 p,float2 a,float2 b) {
  float2 d=b-a;return length(p-a-d*clamp(dot(p-a,d)/dot(d,d),0.0,1.0));
}
fragment float4 ui_fragment(Out in [[stage_in]], texture2d<float> text [[texture(0)]]) {
  if (in.textured==1) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return text.sample(s,in.uv);
  }
  float dist;
  if (in.textured==2 || in.textured==3) {
    float2 p=in.uv*16.0;if(in.textured==3)p.x=16.0-p.x;
    dist=min(segment_distance(p,float2(9,3),float2(4,8)),
         min(segment_distance(p,float2(4,8),float2(9,13)),segment_distance(p,float2(4,8),float2(13,8))))-1.0;
  } else if (in.textured==4) {
    float2 p=in.uv*16.0;float2 q=p-float2(8,8.5);float r=5.5;
    dist=q.x>0 && q.y<0 ? min(length(q-float2(r,0)),length(q-float2(0,-r)))-1.0 : abs(length(q)-r)-1.0;
    dist=min(dist,segment_distance(p,float2(8,3),float2(13.5,3))-1.0);
    dist=min(dist,segment_distance(p,float2(10.5,0.5),float2(13.5,3))-1.0);
    dist=min(dist,segment_distance(p,float2(10.5,5.5),float2(13.5,3))-1.0);
  } else {
    float2 d=abs((in.uv-0.5)*in.size)-(in.size*0.5-in.radius);
    dist=length(max(d,0.0))+min(max(d.x,d.y),0.0)-in.radius;
    if (in.stroke > 0) dist=max(dist,-dist-in.stroke);
  }
  float a=in.color.a*(1-smoothstep(-0.5,0.5,dist));
  return float4(in.color.rgb*a,a);
}
