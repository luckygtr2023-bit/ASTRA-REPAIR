#version 450
// v1.4 REAL deep-sky objects (OpenNGC authority) — soft elliptical glows at
// their TRUE catalog directions and TRUE angular sizes projected onto the
// scene shell (REAL data; the shell radius itself is the scene CINEMATIC
// display scale; redshift is a HUD quantity, never recolors the image).
layout(push_constant) uniform PC {
    mat4 view_proj;
    vec4 cam_right;
    vec4 cam_up;
    vec4 misc;           // x=tan(fov/2), y=viewport_h, z=selected_index(-1), w=unused
} pc;

struct CatDsoViz { vec3 pos; float half_size; vec3 rgb; float flags; };
layout(std430, binding = 0) readonly buffer Dsos { CatDsoViz dsos[]; } ssbo;

layout(location = 0) out vec3 v_rgb;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out float v_flags;

void main() {
    CatDsoViz d = ssbo.dsos[gl_InstanceIndex];
    vec2 corner = vec2((gl_VertexIndex == 1 || gl_VertexIndex == 3) ? 1.0 : -1.0,
                       (gl_VertexIndex >= 2) ? 1.0 : -1.0);
    v_uv = corner;
    vec3 world = d.pos + pc.cam_right.xyz * (corner.x * d.half_size)
                      + pc.cam_up.xyz * (corner.y * d.half_size);
    gl_Position = pc.view_proj * vec4(world, 1.0);
    v_rgb = d.rgb;
    v_flags = d.flags;
}
