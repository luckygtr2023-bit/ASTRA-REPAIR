#version 450
// ASTRA COSMOS — celestial body fragment shader (lit color computed in vertex).

layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(inColor, 1.0);
}
