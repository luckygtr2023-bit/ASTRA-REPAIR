#version 450
// v1.4 REAL catalog stars — instanced billboards from the HYG v4.1 authority.
//
//   DATA: CatStarViz SSBO (32 B/instance, produced CPU-side by
//         catalog::build_star_viz from the sha256-verified astro_stars.v14.bin
//         — REAL directions DOUBLE→(float at this boundary ONLY)).
//         pos_units: celestial-shell scene units (CINEMATIC AA projection of a
//         REAL direction FROM THE OBSERVER, incl. true parallax).
//         size_px: magnitude-driven pixel size (CINEMATIC display mapping of a
//         REAL catalog magnitude).
//         rgb: color from the project star LUT keyed by PHYSICALLY_MODELED
//         temp estimate (white neutral when NOT AVAILABLE).
//   NO invented geometry. NOT AVAILABLE fields render as NaN-flagged dim
//   points, never as fabricated bright stars.
layout(push_constant) uniform PC {
    mat4 view_proj;      // 64
    vec4 cam_right;      // +16  (view-space billboard axes, float scene offsets)
    vec4 cam_up;         // +16
    vec4 misc;           // +16  x=tan(fov/2), y=viewport_h_px, z=selected_index(-1), w=star_boost
} pc;

struct CatStarViz { vec3 pos; float size_px; vec3 rgb; float flags; };
layout(std430, binding = 0) readonly buffer Stars { CatStarViz stars[]; } ssbo;

layout(location = 0) out vec3 v_rgb;
layout(location = 1) out vec2 v_disc;
layout(location = 2) out float v_boost;

void main() {
    CatStarViz s = ssbo.stars[gl_InstanceIndex];
    // triangle-strip quad corners (−1..+1) from the first 4 vertex indices
    vec2 corner = vec2((gl_VertexIndex == 1 || gl_VertexIndex == 3) ? 1.0 : -1.0,
                       (gl_VertexIndex >= 2) ? 1.0 : -1.0);
    v_disc = corner;
    // view-space position to size the billboard in PIXELS (documented mapping)
    vec4 vp = pc.view_proj * vec4(s.pos, 1.0);
    // world size such that projected footprint ≈ size_px on screen
    float world_r = s.size_px * vp.w * pc.misc.x * 2.0 / pc.misc.y;
    bool selected = (gl_InstanceIndex == uint(pc.misc.z));
    if (selected) { world_r *= 1.8; }
    vec3 world = s.pos + pc.cam_right.xyz * (corner.x * world_r)
                      + pc.cam_up.xyz * (corner.y * world_r);
    gl_Position = pc.view_proj * vec4(world, 1.0);
    v_rgb = s.rgb;
    v_boost = selected ? 1.0 : pc.misc.w;
}
