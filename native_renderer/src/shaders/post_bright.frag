#version 450
// ASTRA COSMOS v0.5 — bloom bright-pass (HDR domain). Threshold policy from
// render_math.h (CINEMATIC display transform; never mutates scientific state).
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(push_constant) uniform BrightPC { vec4 params; } pc; // x = threshold, y = soft knee

void main() {
    vec3 c = texture(hdrColor, inUV).rgb;
    float l = max(c.r, max(c.g, c.b));
    float t = pc.params.x;
    float knee = 0.5 * pc.params.y + 1e-4;   // soft knee around threshold
    float soft = clamp(l - t + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0f * knee);
    float w = max(soft, l - t) / max(l, 1e-4);
    outColor = vec4(c * max(w, 0.0), 1.0);
}
