#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "clip.glsl"
layout(set=0,binding=0) uniform sampler2D source;
layout(location=0) in vec2 local;
layout(location=0) out vec4 result;
void main() {
    result = texture(source, local / pc.surface.xy) * pc.params.y * clip_coverage();
}
