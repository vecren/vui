#include "shape.glsl"
struct ClipData { vec4 rect; vec4 row0; vec4 row1; vec4 params; vec4 radii; };
layout(std430,set=0,binding=1) readonly buffer Clips { ClipData clips[]; };
float clip_coverage() {
    float result = 1;
    int index = int(pc.params.z);
    vec3 screen = vec3(gl_FragCoord.xy / vec2(pc.row0.w, pc.row1.w), 1);
    while (index >= 0) {
        ClipData c = clips[index];
        if (c.params.z == 0) return 0;
        vec2 local = vec2(dot(c.row0.xyz,screen),dot(c.row1.xyz,screen));
        result *= shape_coverage(local, c.rect, c.params.x, c.radii);
        index = int(c.params.y);
    }
    return result;
}
