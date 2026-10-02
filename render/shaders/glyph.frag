#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(set=0,binding=0) uniform sampler2D source;
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;
void main() {
    vec2 uv = pc.uv.xy + (local - pc.rect.xy) / pc.rect.zw * pc.uv.zw;
    result = pc.color * texture(source, uv).r * clip_coverage();
}
