#version 440

// Convierte un fotograma NV12 (plano Y + plano UV) a RGB.

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 transform;
    mat4 yuvToRgb; // matriz de color del video (BT.709, BT.601…) y rango
} ubuf;

layout(binding = 1) uniform sampler2D texY;
layout(binding = 2) uniform sampler2D texUV;

void main()
{
    float y = texture(texY, v_texcoord).r;
    vec2 uv = texture(texUV, v_texcoord).rg;
    vec3 rgb = (ubuf.yuvToRgb * vec4(y, uv, 1.0)).rgb;
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
