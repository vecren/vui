#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(set=0,binding=0) uniform sampler2D source;
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;
void main() {
    vec2 uv = pc.uv.xy + clamp((local - pc.rect.xy) / pc.rect.zw, 0, 1) * pc.uv.zw;
    float alpha = coverage(rounded_distance(local, pc.rect, 0)) * clip_coverage();
    vec4 value = texture(source, uv);
    if (pc.params.x > 0.5)
        value.rgb = mix(value.rgb / 12.92, pow(max((value.rgb + 0.055) / 1.055, 0), vec3(2.4)),
                        greaterThan(value.rgb, vec3(0.04045)));
    if (pc.params.y > 1.5)
        value.a = 1;
    else if (pc.params.y > 0.5)
        value.rgb *= value.a;
    result = value * alpha;
}
