#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    float sim_time;
    float width;
    float height;
    float frame;
} pc;

void main() {
    // Generate a beautiful deep space gradient based on sim time and UV
    vec2 center = vec2(0.5, 0.5);
    vec2 unorm = inUV;
    
    // Core glow (simulates central star/accretion)
    float d = distance(unorm, center);
    float glow = 0.05 / (d + 0.01);
    
    // Colors that pulse over time
    float r = glow * (0.8 + 0.2 * sin(pc.sim_time * 0.5));
    float g = glow * (0.3 + 0.1 * sin(pc.sim_time * 0.7));
    float b = glow * (0.1 + 0.05 * sin(pc.sim_time * 0.9));
    
    // Add grid pattern to verify resolution
    float grid = 0.0;
    if (mod(inUV.x * pc.width, 100.0) < 1.0 || mod(inUV.y * pc.height, 100.0) < 1.0) {
        grid = 0.1;
    }
    
    outColor = vec4(r + grid, g + grid, b + grid, 1.0);
}
