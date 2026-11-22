#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(set=0,binding=0) uniform sampler2D filtered_image;
layout(set=0,binding=3) uniform sampler2D original_image;
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;

void main() {
    float mask = coverage(rounded_distance(local, pc.rect, pc.params.x)) * clip_coverage();
    if (mask <= 0)
        discard;
    vec2 pixel = gl_FragCoord.xy - pc.uv.xy;
    vec4 original = texelFetch(original_image, ivec2(pixel), 0);
    vec4 filtered = texture(filtered_image, pixel / vec2(textureSize(original_image, 0)));
    result = mix(original, filtered, mask);
}
