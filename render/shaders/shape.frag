#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;
void main() {
    float alpha = shape_coverage(local, pc.rect, pc.params.x, pc.radial);
    if (pc.params.y >= 0) {
        float width = pc.params.y;
        vec4 inner = pc.rect + vec4(width, width, -2 * width, -2 * width);
        alpha = max(0, alpha - shape_coverage(local, inner, pc.params.x, pc.uv));
    }
    result = pc.color * (alpha * clip_coverage());
}
