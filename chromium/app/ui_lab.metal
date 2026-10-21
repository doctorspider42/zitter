#include <metal_stdlib>
using namespace metal;
struct Rect { float x, y, width, height; uint color; };
struct Out { float4 position [[position]]; float4 color; };
vertex Out ui_vertex(uint v [[vertex_id]], uint i [[instance_id]],
                     const device Rect* rects [[buffer(0)]],
                     constant float2& viewport [[buffer(1)]]) {
  const float2 corners[6] = {float2(0,0), float2(1,0), float2(0,1),
                             float2(0,1), float2(1,0), float2(1,1)};
  Rect r = rects[i];
  float2 p = float2(r.x, r.y) + corners[v] * float2(r.width, r.height);
  Out o;
  o.position = float4(p.x / viewport.x * 2 - 1, 1 - p.y / viewport.y * 2, 0, 1);
  o.color = float4(float((r.color >> 16) & 255), float((r.color >> 8) & 255),
                   float(r.color & 255), float(r.color >> 24)) / 255.0;
  return o;
}
fragment float4 ui_fragment(Out in [[stage_in]]) { return in.color; }
