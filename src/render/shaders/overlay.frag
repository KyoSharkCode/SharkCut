#version 440

// Imagen RGBA con alfa premultiplicado (un subtítulo) sobre el lienzo.

layout(location = 0) in vec2 v_uv;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 p0; // x: opacidad
    vec4 p1;
    vec4 p2;
} ubuf;

layout(binding = 1) uniform sampler2D tex;

void main()
{
    fragColor = texture(tex, v_uv) * ubuf.p0.x;
}
