#version 450
// ASTRA COSMOS — velocity vector overlay.
// Draws a single line segment from a body position along its SIMULATED
// velocity (vis direction, scaled for visibility — CINEMATIC scale,
// scientific value shown verbatim in the inspector).

layout(push_constant) uniform VectorPC {
    mat4 viewProj;
    vec4 originScale; // xyz = body render position (target-relative), w = vector scale
    vec4 velocity;    // xyz = velocity km/s (real value), unused
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    if (gl_VertexIndex == 0) {
        gl_Position = pc.viewProj * vec4(pc.originScale.xyz, 1.0);
    } else {
        vec3 tip = pc.originScale.xyz + normalize(pc.velocity.xyz) * pc.originScale.w;
        gl_Position = pc.viewProj * vec4(tip, 1.0);
    }
    outColor = pc.color;
}
