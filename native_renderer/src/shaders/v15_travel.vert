#version 450
// v1.5 EXTREME SPACETIME travel overlay — instanced billboards from the
// VALIDATED journey mirror (app/extreme_sim.cpp; authority: astra.theoretical).
//
//   DATA: TravelMark SSBO (32 B/mark), produced CPU-side per frame from the
//         journey plan/FSM (double→float at THIS boundary only).
//         pos: floating-origin render units (same policy as bodies/catalog).
//         size_px: CINEMATIC display size constant (the chart geometry is
//                  otherwise invisible at AU scale — this amplification is a
//                  VIEW parameter, never a physical claim; HUD shows the real
//                  numeric parameters).
//         rgb: type color (mouth/in-mouth vs tunnel vs bubble vs observer).
//         flags.bit0..3: kind (0 mouth-ring, 1 tunnel, 2 warp-bubble,
//                        3 observer marker, 4 displacement line)
//   NOTHING here invents geometry: every position is plan-derived.
layout(push_constant) uniform PC {
    mat4 view_proj;
    vec4 cam_right;
    vec4 cam_up;
    vec4 misc;   // x=tan(fov/2), y=viewport_h_px, z=reserved, w=reserved
} pc;

struct TravelMark { vec3 pos; float size_px; vec3 rgb; float flags; };
layout(std430, binding = 0) readonly buffer Marks { TravelMark marks[]; } ssbo;

layout(location = 0) out vec3 v_rgb;
layout(location = 1) out vec2 v_disc;
layout(location = 2) out float v_kind;

void main() {
    TravelMark s = ssbo.marks[gl_InstanceIndex];
    vec2 corner = vec2((gl_VertexIndex == 1 || gl_VertexIndex == 3) ? 1.0 : -1.0,
                       (gl_VertexIndex >= 2) ? 1.0 : -1.0);
    v_disc = corner;
    v_rgb = s.rgb;
    v_kind = s.flags;
    vec4 vp = pc.view_proj * vec4(s.pos, 1.0);
    float world_r = s.size_px * vp.w * pc.misc.x * 2.0 / pc.misc.y;
    vec3 world = s.pos + (pc.cam_right.xyz * corner.x + pc.cam_up.xyz * corner.y) * world_r;
    gl_Position = pc.view_proj * vec4(world, 1.0);
}
