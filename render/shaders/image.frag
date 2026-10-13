#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(set=0,binding=0) uniform sampler2D source;
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;
void main() {
    vec2 uv = pc.uv.xy + clamp((local - pc.rect.xy) / pc.rect.zw, 0, 1) * pc.uv.zw;
    if (pc.params.w > 0.5) {
        vec2 half_texel = 0.5 / vec2(textureSize(source, 0));
        vec2 middle = pc.uv.xy + pc.uv.zw * 0.5;
        uv = clamp(uv, min(pc.uv.xy + half_texel, middle),
                   max(pc.uv.xy + pc.uv.zw - half_texel, middle));
    }
    uint edges = uint(pc.image_edges);
    float distance = edges == 15u ? rounded_distance(local, pc.rect, 0) : -1e10;
    if (edges != 15u && (edges & 1u) != 0u) distance = max(distance, pc.rect.x - local.x);
    if (edges != 15u && (edges & 2u) != 0u) distance = max(distance, pc.rect.y - local.y);
    if (edges != 15u && (edges & 4u) != 0u) distance = max(distance, local.x - pc.rect.x - pc.rect.z);
    if (edges != 15u && (edges & 8u) != 0u) distance = max(distance, local.y - pc.rect.y - pc.rect.w);
    float alpha = (edges == 0u ? 1 : coverage(distance)) * clip_coverage();
    vec4 value = texture(source, uv);
    if (pc.params.x > 0.5)
        value.rgb = mix(value.rgb / 12.92, pow(max((value.rgb + 0.055) / 1.055, 0), vec3(2.4)),
                        greaterThan(value.rgb, vec3(0.04045)));
    if (pc.params.y > 1.5)
        value.a = 1;
    else if (pc.params.y > 0.5)
        value.rgb *= value.a;
    result = value * pc.color * alpha;
}
