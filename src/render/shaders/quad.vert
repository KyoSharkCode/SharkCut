#version 440

// Cuadro a pantalla completa para las pasadas de composición y presentación.
// v_uv está en coordenadas visuales: (0, 0) arriba a la izquierda en todas las API.

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 p0;
    vec4 p1;
    vec4 p2;
} ubuf;

void main()
{
    v_uv = texcoord;
    gl_Position = ubuf.mvp * vec4(position, 0.0, 1.0);
}
