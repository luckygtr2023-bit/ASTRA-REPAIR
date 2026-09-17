#version 450
// ASTRA COSMOS v0.5 — bloom separable 9-tap gaussian (optimized linear
// sampling weights for sigma~1.6; weights 0.227027,0.1945946,0.1216216,
// 0.054054,0.016216, offsets 1.3846153846,3.2307692308 texels).
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform BlurPC { vec4 dir; } pc; // xy = texel step direction (1/w,0) or (0,1/h)

void main() {
    vec2 o = pc.dir.xy;
    vec3 c = texture(tex, inUV).rgb * 0.2270270270;
    c += texture(tex, inUV + o * 1.3846153846).rgb * 0.3162162162;
    c += texture(tex, inUV - o * 1.3846153846).rgb * 0.3162162162;
    c += texture(tex, inUV + o * 3.2307692308).rgb * 0.0702702703;
    c += texture(tex, inUV - o * 3.2307692308).rgb * 0.0702702703;
    outColor = vec4(c, 1.0);
}
