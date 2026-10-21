#version 440

// Una capa del lienzo: un fotograma NV12 (plano Y + plano UV) con su transparencia (texA;
// un píxel blanco si el video es opaco) y la opacidad del clip, en alfa premultiplicado.
// Efectos: pantalla verde, blanco y negro, desenfoque y esquinas redondeadas.
//
// Modo sombra (params.w = 1): en vez del clip se dibuja su sombra o resplandor, con la
// forma del clip (esquinas, transparencia y pantalla verde) difuminada, desplazada y del
// color elegido. El compositor la dibuja antes que el clip, con la geometría ampliada.

layout(location = 0) in vec2 v_texcoord;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 transform;
    mat4 yuvToRgb; // matriz de color del video (BT.709, BT.601…) y rango
    vec4 posRect;
    vec4 uvRect;
    vec4 params;    // x: opacidad, y: blanco y negro (0–1), z: radio de las esquinas (px), w: 1 = sombra
    vec4 key;       // xy: tono del color que se quita (Cb, Cr / (luma + 0,1)); z: 1 = activa
    vec4 keyParams; // x: similitud, y: suavidad, z: reflejo (relativas a la saturación del color)
    vec4 box;       // xy: tamaño de la parte visible (px del lienzo); zw: radio del desenfoque (u, v)
    vec4 cropUv;    // parte visible en la textura: u0, v0, u1, v1
    vec4 shadowColor; // rgb y opacidad de la sombra
    vec4 shadow;    // xy: difuminado (u, v); zw: desplazamiento (u, v)
    vec4 extra;     // x: crecimiento de la sombra (px)
} ubuf;

layout(binding = 1) uniform sampler2D texY;
layout(binding = 2) uniform sampler2D texUV;
layout(binding = 3) uniform sampler2D texA;

const vec3 kLuma = vec3(0.2126, 0.7152, 0.0722); // BT.709
const int kTaps = 32;          // muestras del desenfoque (disco de Vogel)
const float kGolden = 2.399963; // ángulo áureo

vec3 toRgb(float y, vec2 uv) {
    return clamp((ubuf.yuvToRgb * vec4(y, uv, 1.0)).rgb, 0.0, 1.0);
}

// Punto i del disco de Vogel (radio 1): muestras repartidas sin dibujar anillos.
vec2 vogel(int i) {
    float fi = float(i);
    float r = sqrt((fi + 0.5) / float(kTaps));
    float a = fi * kGolden;
    return vec2(cos(a), sin(a)) * r;
}

// Opacidad que deja la pantalla verde (1 si no está activa). `chroma` sale sin reflejo.
float keyAlpha(vec3 rgb, out vec3 cleaned) {
    cleaned = rgb;
    if (ubuf.key.z < 0.5) {
        return 1.0;
    }
    float luma = dot(rgb, kLuma);
    vec2 chroma = vec2((rgb.b - luma) / 1.8556, (rgb.r - luma) / 1.5748);
    // Tono relativo al brillo: el fondo más iluminado y el de las sombras dan casi lo
    // mismo. El +0,1 acerca los píxeles muy oscuros al gris (su tono es ruido).
    vec2 hue = chroma / (luma + 0.1);
    float keyLength = length(ubuf.key.xy);
    float d = distance(hue, ubuf.key.xy) / keyLength; // 1 = como un gris
    float similarity = ubuf.keyParams.x;
    float alpha = smoothstep(similarity, similarity + max(ubuf.keyParams.y, 0.001), d);
    // Reflejo: cerca del color quitado se le resta su tono (sin tocar la luminancia).
    vec2 dir = ubuf.key.xy / keyLength;
    float spill = 1.0 - smoothstep(similarity, similarity + max(ubuf.keyParams.z, 0.001), d);
    chroma -= dir * max(dot(chroma, dir), 0.0) * spill;
    float r = luma + 1.5748 * chroma.y;
    float b = luma + 1.8556 * chroma.x;
    float g = (luma - kLuma.r * r - kLuma.b * b) / kLuma.g;
    cleaned = clamp(vec3(r, g, b), 0.0, 1.0);
    return alpha;
}

// 1 dentro del rectángulo visible con las esquinas redondeadas (agrandado `grow` px),
// 0 fuera, con un píxel de transición.
float roundedMask(vec2 uv, float grow) {
    vec2 local = (uv - ubuf.cropUv.xy) / max(ubuf.cropUv.zw - ubuf.cropUv.xy, vec2(1e-6));
    vec2 p = (local - 0.5) * ubuf.box.xy;
    vec2 halfSize = ubuf.box.xy * 0.5 + vec2(grow);
    float r = min(ubuf.params.z + grow, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + vec2(r);
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    return clamp(0.5 - d, 0.0, 1.0);
}

// Forma del clip en `uv` (para la sombra): transparencia, pantalla verde y esquinas.
float shapeAt(vec2 uv) {
    vec2 inside = clamp(uv, ubuf.cropUv.xy, ubuf.cropUv.zw);
    vec3 cleaned;
    float a = texture(texA, inside).r * keyAlpha(toRgb(texture(texY, inside).r,
                                                       texture(texUV, inside).rg), cleaned);
    return a * roundedMask(uv, ubuf.extra.x);
}

void main()
{
    if (ubuf.params.w > 0.5) {
        vec2 base = v_texcoord - ubuf.shadow.zw;
        float shape = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            shape += shapeAt(base + vogel(i) * ubuf.shadow.xy);
        }
        float alpha = shape / float(kTaps) * ubuf.shadowColor.a * ubuf.params.x;
        fragColor = vec4(ubuf.shadowColor.rgb * alpha, alpha);
        return;
    }

    float y;
    vec2 uv;
    float a;
    if (ubuf.box.z > 0.0 || ubuf.box.w > 0.0) {
        y = 0.0;
        uv = vec2(0.0);
        a = 0.0;
        for (int i = 0; i < kTaps; ++i) {
            vec2 s = v_texcoord + vogel(i) * ubuf.box.zw;
            y += texture(texY, s).r;
            uv += texture(texUV, s).rg;
            a += texture(texA, s).r;
        }
        y /= float(kTaps);
        uv /= float(kTaps);
        a /= float(kTaps);
    } else {
        y = texture(texY, v_texcoord).r;
        uv = texture(texUV, v_texcoord).rg;
        a = texture(texA, v_texcoord).r;
    }
    vec3 rgb;
    float alpha = a * ubuf.params.x * keyAlpha(toRgb(y, uv), rgb);
    rgb = mix(rgb, vec3(dot(rgb, kLuma)), ubuf.params.y);
    if (ubuf.params.z > 0.0) {
        alpha *= roundedMask(v_texcoord, 0.0);
    }
    fragColor = vec4(rgb * alpha, alpha);
}
