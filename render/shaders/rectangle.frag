#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;
void main() {
    float alpha = coverage(rounded_distance(local,pc.rect,pc.params.x)) * clip_coverage();
    result = pc.color * alpha;
}
