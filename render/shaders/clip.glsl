struct ClipData { vec4 rect; vec4 row0; vec4 row1; vec4 params; };
layout(std430,set=0,binding=1) readonly buffer Clips { ClipData clips[]; };
float rounded_distance(vec2 p, vec4 rect, float radius) {
    float r = min(radius, min(rect.z,rect.w)*0.5);
    vec2 q = abs(p-rect.xy-rect.zw*0.5) - (rect.zw*0.5-r);
    return length(max(q,0)) + min(max(q.x,q.y),0) - r;
}
float coverage(float distance) { return clamp(0.5-distance/max(fwidth(distance),0.0001),0,1); }
float clip_coverage() {
    float result = 1;
    int index = int(pc.params.z);
    vec3 screen = vec3(gl_FragCoord.xy / pc.surface.z, 1);
    while (index >= 0) {
        ClipData c = clips[index];
        if (c.params.z == 0) return 0;
        vec2 local = vec2(dot(c.row0.xyz,screen),dot(c.row1.xyz,screen));
        result *= coverage(rounded_distance(local,c.rect,c.params.x));
        index = int(c.params.y);
    }
    return result;
}
