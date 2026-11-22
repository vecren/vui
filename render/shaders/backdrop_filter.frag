#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=0,binding=0) uniform sampler2D source_image;
layout(location=0) out vec4 result;

void main() {
    int radius = int(pc.color.w);
    if (pc.params.y != 0) {
        result = texelFetch(source_image, ivec2(gl_FragCoord.xy + pc.uv.xy), 0);
        return;
    }
    vec2 pixel_size = 1.0 / vec2(textureSize(source_image, 0));
    vec2 center = gl_FragCoord.xy * pc.uv.xy * pixel_size;
    if (radius == 0) {
        result = texture(source_image, center);
        return;
    }
    vec2 axis = pc.color.xy * pixel_size;
    float ratio = exp(-0.5 / (pc.color.z * pc.color.z));
    float decay = ratio * ratio;
    float weight = 1;
    float sum = 1;
    vec4 color = texture(source_image, center);
    for (int tap = 1; tap <= radius; ++tap) {
        weight *= ratio;
        ratio *= decay;
        vec2 delta = float(tap) * axis;
        color += weight * (texture(source_image, center + delta) +
                           texture(source_image, center - delta));
        sum += 2 * weight;
    }
    result = color / sum;
}
