#version 450
#extension GL_GOOGLE_include_directive : require
#define VUI_VERTEX
#include "common.glsl"
layout(location=0) out vec2 local;
void main() {
    instance_index = gl_InstanceIndex;
    const vec2 corners[6] = vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
    vec2 margin = vec2(0);
    float det = pc.row0.x * pc.row1.y - pc.row0.y * pc.row1.x;
    if (pc.params.w == 0 && det != 0)
        margin = vec2(abs(pc.row1.y)+abs(pc.row0.y), abs(pc.row1.x)+abs(pc.row0.x)) / abs(det) / pc.surface.z;
    local = pc.rect.xy - margin + corners[gl_VertexIndex] * (pc.rect.zw + margin * 2);
    vec3 p = vec3(local, 1);
    vec2 position = vec2(dot(pc.row0.xyz,p), dot(pc.row1.xyz,p));
    gl_Position = vec4(position / pc.surface.xy * 2 - 1, 0, 1);
}
