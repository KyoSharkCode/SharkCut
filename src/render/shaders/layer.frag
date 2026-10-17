#version 440

// Convierte un fotograma NV12 (plano Y + plano UV) a RGB, con su transparencia (texA; un
// píxel blanco si el video es opaco) y la opacidad del clip (alfa premultiplicado: se
// mezcla sobre lo que ya hay en el lienzo). Con la pantalla verde activa, el color de
// fondo se vuelve transparente.

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 transform;
    mat4 yuvToRgb; // matriz de color del video (BT.709, BT.601…) y rango
    vec4 posRect;
    vec4 uvRect;
    vec4 params;    // x: opacidad
    vec4 key;       // xy: tono del color que se quita (Cb, Cr / (luma + 0,1)); z: 1 = activa
    vec4 keyParams; // x: similitud, y: suavidad, z: reflejo (relativas a la saturación del color)
} ubuf;

layout(binding = 1) uniform sampler2D texY;
layout(binding = 2) uniform sampler2D texUV;
layout(binding = 3) uniform sampler2D texA;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722); // BT.709

void main()
{
    float y = texture(texY, v_texcoord).r;
    vec2 uv = texture(texUV, v_texcoord).rg;
    vec3 rgb = clamp((ubuf.yuvToRgb * vec4(y, uv, 1.0)).rgb, 0.0, 1.0);
    float alpha = texture(texA, v_texcoord).r * ubuf.params.x;

    if (ubuf.key.z > 0.5) {
        float luma = dot(rgb, kLuma);
        vec2 chroma = vec2((rgb.b - luma) / 1.8556, (rgb.r - luma) / 1.5748);
        // Tono relativo al brillo: el fondo más iluminado y el de las sombras dan casi lo
        // mismo. El +0,1 acerca los píxeles muy oscuros al gris (su tono es ruido).
        vec2 hue = chroma / (luma + 0.1);
        float keyLength = length(ubuf.key.xy);
        float d = distance(hue, ubuf.key.xy) / keyLength; // 1 = como un gris
        float similarity = ubuf.keyParams.x;
        alpha *= smoothstep(similarity, similarity + max(ubuf.keyParams.y, 0.001), d);
        // Reflejo: cerca del color quitado se le resta su tono (sin tocar la luminancia).
        vec2 dir = ubuf.key.xy / keyLength;
        float spill = 1.0 - smoothstep(similarity, similarity + max(ubuf.keyParams.z, 0.001), d);
        chroma -= dir * max(dot(chroma, dir), 0.0) * spill;
        float r = luma + 1.5748 * chroma.y;
        float b = luma + 1.8556 * chroma.x;
        float g = (luma - kLuma.r * r - kLuma.b * b) / kLuma.g;
        rgb = clamp(vec3(r, g, b), 0.0, 1.0);
    }
    fragColor = vec4(rgb * alpha, alpha);
}
