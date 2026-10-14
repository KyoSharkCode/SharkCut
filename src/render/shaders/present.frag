#version 440

// Dibuja el lienzo ya compuesto en el visor.

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 p0;
    vec4 p1; // z: 1 si las texturas renderizadas tienen el eje Y invertido (OpenGL)
    vec4 p2;
} ubuf;

layout(binding = 1) uniform sampler2D canvasTex;

void main()
{
    vec2 uv = ubuf.p1.z > 0.5 ? vec2(v_uv.x, 1.0 - v_uv.y) : v_uv;
    fragColor = vec4(texture(canvasTex, uv).rgb, 1.0);
}
