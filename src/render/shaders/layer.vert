#version 440

// Cuadro de un clip en el lienzo: ajustado sin deformar, con su posición, escala y
// recorte (las coordenadas de textura y la geometría se recortan a la vez).

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 0) uniform buf {
    mat4 transform; // corrección de la API gráfica + ajuste, escala y posición
    mat4 yuvToRgb;
    vec4 posRect;   // parte visible en coordenadas del video: izquierda, abajo, derecha, arriba
    vec4 uvRect;    // la misma parte en la textura: u0, v0 (arriba), u1, v1 (abajo)
    vec4 params;    // x: opacidad
} ubuf;

void main()
{
    v_texcoord = mix(ubuf.uvRect.xy, ubuf.uvRect.zw, texcoord);
    vec2 p = mix(ubuf.posRect.xy, ubuf.posRect.zw, position * 0.5 + 0.5);
    gl_Position = ubuf.transform * vec4(p, 0.0, 1.0);
}
