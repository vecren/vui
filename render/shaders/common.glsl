struct DrawData {
    vec4 rect;
    vec4 color;
    vec4 row0;
    vec4 row1;
    vec4 params;
    vec4 surface;
    vec4 uv;
};
layout(std430,set=0,binding=2) readonly buffer Instances { DrawData instances[]; };
#ifdef VUI_VERTEX
layout(location=1) flat out uint instance_index;
#define pc instances[gl_InstanceIndex]
#else
layout(location=1) flat in uint instance_index;
#define pc instances[instance_index]
#endif
