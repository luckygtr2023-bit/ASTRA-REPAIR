#version 450

// Full-screen triangle without vertex buffer
layout(location = 0) out vec2 outUV;

void main() {
    float x = -1.0 + float((gl_VertexIndex & 1) << 2);
    float y = -1.0 + float((gl_VertexIndex & 2) << 1);
    outUV = vec2((x+1.0)*0.5, (y+1.0)*0.5);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
