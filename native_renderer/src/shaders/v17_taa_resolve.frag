#version 450
// ASTRA COSMOS v1.7 — TAA-class temporal resolve (CINEMATIC display
// transform only; scientific state untouched). Static-object reprojection:
// reconstructs the previous-frame clip position of the current pixel from
// depth + inverse matrices — NO velocity buffer (documented limitation:
// independently moving objects get no motion vector; camera-only motion is
// resolved correctly). Neighborhood clamp (3x3 min/max) bounds history
// to current-frame support; alpha blends 10% current.
// STATUS: shader authored + compile-verified (glslangValidator). Pipeline
// integration is gated on the Windows/Vulkan run — NOT VERIFIED until then.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D currentTex;
layout(set = 0, binding = 1) uniform sampler2D historyTex;
layout(set = 0, binding = 2) uniform sampler2D depthTex;

layout(push_constant) uniform TaaPC {
    mat4 invViewProj;       // current frame inverse view*proj (with jitter)
    mat4 prevViewProj;      // previous frame view*proj (with its jitter)
    vec4 params;            // x = blend (0.1), y = first-frame flag (1 -> bypass history)
} pc;

vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = pc.invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    vec3 curr = texture(currentTex, inUV).rgb;

    // 3x3 neighborhood clamp
    ivec2 sz = textureSize(currentTex, 0);
    vec2 texel = 1.0 / vec2(sz);
    vec3 lo = curr, hi = curr;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            vec3 c = texture(currentTex, inUV + vec2(dx, dy) * texel).rgb;
            lo = min(lo, c);
            hi = max(hi, c);
        }

    if (pc.params.y > 0.5) { outColor = vec4(curr, 1.0); return; }

    float depth = texture(depthTex, inUV).r;
    vec3 world = worldFromDepth(inUV, depth);
    vec4 prevClip = pc.prevViewProj * vec4(world, 1.0);
    if (abs(prevClip.w) < 1e-8) { outColor = vec4(curr, 1.0); return; }
    vec2 prevUV = prevClip.xy / prevClip.w * 0.5 + 0.5;
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        outColor = vec4(curr, 1.0);
        return;
    }
    vec3 hist = clamp(texture(historyTex, prevUV).rgb, lo, hi);
    outColor = vec4(mix(hist, curr, pc.params.x), 1.0);
}
