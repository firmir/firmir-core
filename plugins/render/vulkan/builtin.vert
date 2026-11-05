#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUv;

layout(push_constant) uniform PushConstants {
    vec2 targetSize;
    int textured;
    int padding;
} pushConstants;

void main() {
    vec2 ndc = inPosition / pushConstants.targetSize * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outColor = inColor;
    outUv = inUv;
}
