#version 440

// Cuadro que ocupa el área del video (ajustada a la relación de aspecto).

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform buf {
    mat4 transform; // corrección de la API gráfica + ajuste de aspecto
    mat4 yuvToRgb;
} ubuf;

void main()
{
    v_texcoord = texcoord;
    gl_Position = ubuf.transform * vec4(position, 0.0, 1.0);
}
