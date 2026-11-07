#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUv;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D sourceTexture;

layout(push_constant) uniform PushConstants {
    vec2 targetSize;
    int textured;
    int padding;
} pushConstants;

void main() {
    outColor = pushConstants.textured != 0
        ? inColor * texture(sourceTexture, inUv)
        : inColor;
}
