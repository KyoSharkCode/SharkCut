#version 440

// Transiciones entre la capa A (clip saliente) y la capa B (clip entrante).
// Técnicas propias, genéricas: transformaciones animadas, desenfoques y deformaciones.

layout(location = 0) in vec2 v_uv; // (0, 0) arriba a la izquierda
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 p0; // x: modo, y: progreso con curva, z: velocidad de la curva, w: intensidad
    vec4 p1; // x: desenfoque, y: relación de aspecto (ancho/alto), z: invertir Y, w: progreso lineal
    vec4 p2; // reservado
} ubuf;

layout(binding = 1) uniform sampler2D texA;
layout(binding = 2) uniform sampler2D texB;

const float PI = 3.14159265359;
const int TAPS = 24;

// Modos (mismo orden que sharkcut::TransitionType; -1 = sin transición).
const int NONE = -1;
const int FADE_TO_BLACK = 0;
const int PAN_LEFT = 1;
const int ROTATE = 2;
const int SHAKE_DISTORT = 3;
const int ZOOM_IN = 4;
const int ZOOM_OUT = 5;
const int ZOOM_ROTATE_OUT = 6;

float aspect() { return ubuf.p1.y; }

// Bordes en espejo: al alejar o girar, la imagen se refleja en lugar de dejar huecos.
vec2 mirrorUv(vec2 uv)
{
    return vec2(1.0) - abs(vec2(1.0) - mod(uv, 2.0));
}

vec4 sampleLayer(int layer, vec2 uv)
{
    uv = mirrorUv(uv);
    if (ubuf.p1.z > 0.5) {
        uv.y = 1.0 - uv.y; // texturas renderizadas en OpenGL: eje Y hacia arriba
    }
    return layer == 0 ? texture(texA, uv) : texture(texB, uv);
}

// Coordenadas centradas con la relación de aspecto corregida (los giros no deforman).
vec2 toCentered(vec2 uv) { return (uv - 0.5) * vec2(aspect(), 1.0); }
vec2 fromCentered(vec2 p) { return p / vec2(aspect(), 1.0) + 0.5; }

vec2 rotate(vec2 p, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// Muestra una capa transformada (escala, giro y desplazamiento alrededor del centro)
// con desenfoque: radial (zoom), de giro y direccional. Los desenfoques reparten
// TAPS muestras a lo largo del movimiento de ese instante.
vec4 transformed(int layer, vec2 uv, float scale, float angle, vec2 offset,
                 float zoomBlur, float spinBlur, vec2 dirBlur)
{
    vec2 p = toCentered(uv);
    bool blurred = abs(zoomBlur) > 1e-4 || abs(spinBlur) > 1e-4 || length(dirBlur) > 1e-4;
    if (!blurred) {
        vec2 q = rotate(p, -angle) / scale - offset;
        return sampleLayer(layer, fromCentered(q));
    }
    vec4 sum = vec4(0.0);
    for (int i = 0; i < TAPS; ++i) {
        float f = float(i) / float(TAPS - 1) - 0.5;
        float s = scale * (1.0 + zoomBlur * f);
        float a = angle + spinBlur * f;
        vec2 o = offset + dirBlur * f;
        vec2 q = rotate(p, -a) / s - o;
        sum += sampleLayer(layer, fromCentered(q));
    }
    return sum / float(TAPS);
}

// Ruido suave y determinista (el mismo fotograma siempre tiembla igual).
float wobble(float t, float seed)
{
    return (sin(t * 91.7 + seed) + 0.6 * sin(t * 233.1 + seed * 1.7) +
            0.3 * sin(t * 517.3 + seed * 2.3)) / 1.9;
}

void main()
{
    int mode = int(ubuf.p0.x + 0.5) - 1; // se envía modo + 1 para evitar negativos
    float e = ubuf.p0.y;          // progreso con curva (0–1)
    float speed = ubuf.p0.z;      // velocidad de la curva (1 = lineal)
    float intensity = ubuf.p0.w;  // 0–1
    float blurAmount = ubuf.p1.x; // 0–1
    float linear = ubuf.p1.w;     // progreso lineal (0–1)
    vec2 uv = v_uv;

    // Velocidad normalizada: con easeInOutExpo el centro va ~7 veces más rápido.
    float motion = blurAmount * clamp(speed / 7.0, 0.0, 1.0);

    // Mitades: la primera mueve A hasta el corte y la segunda asienta B.
    float tA = clamp(e * 2.0, 0.0, 1.0);
    float tB = clamp(e * 2.0 - 1.0, 0.0, 1.0);
    bool firstHalf = e < 0.5;

    if (mode == NONE) {
        fragColor = sampleLayer(0, uv);
    } else if (mode == FADE_TO_BLACK) {
        // Fundido a negro con una pausa en negro que crece con la intensidad.
        float hold = 0.25 * intensity;
        float outEnd = 0.5 - hold;
        float inStart = 0.5 + hold;
        vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
        if (e < outEnd) {
            color = sampleLayer(0, uv) * (1.0 - e / max(outEnd, 1e-4));
        } else if (e > inStart) {
            color = sampleLayer(1, uv) * ((e - inStart) / max(1.0 - inStart, 1e-4));
        }
        fragColor = vec4(color.rgb, 1.0);
    } else if (mode == PAN_LEFT) {
        // A sale por la izquierda y B entra por la derecha, con estela horizontal y un
        // ligero alejamiento a mitad del recorrido (más con más intensidad). Cada muestra
        // del desenfoque decide si cae en A o en B: la costura se emborrona bien.
        float depth = 1.0 - 0.25 * intensity * sin(PI * e);
        vec2 base = fromCentered(toCentered(uv) / depth);
        float trail = motion * 0.35;
        vec4 sum = vec4(0.0);
        int count = 0;
        for (int i = 0; i < TAPS; ++i) {
            if (trail <= 1e-4 && i > 0) {
                break;
            }
            float f = trail <= 1e-4 ? 0.0 : float(i) / float(TAPS - 1) - 0.5;
            float x = base.x + e + trail * f; // posición en la escena A|B
            sum += x < 1.0 ? sampleLayer(0, vec2(x, base.y)) : sampleLayer(1, vec2(x - 1.0, base.y));
            count += 1;
        }
        fragColor = sum / float(count);
    } else if (mode == ROTATE) {
        float maxAngle = intensity * PI * 0.5;
        if (firstHalf) {
            float angle = tA * maxAngle;
            fragColor = transformed(0, uv, 1.0 + 0.6 * intensity * tA, angle, vec2(0.0),
                                    0.0, motion * 0.6, vec2(0.0));
        } else {
            float angle = -(1.0 - tB) * maxAngle;
            fragColor = transformed(1, uv, 1.0 + 0.6 * intensity * (1.0 - tB), angle,
                                    vec2(0.0), 0.0, motion * 0.6, vec2(0.0));
        }
    } else if (mode == SHAKE_DISTORT) {
        // Temblor que crece hasta el corte y se apaga, con lente deformada y
        // separación de colores. El corte de A a B ocurre en el centro.
        float envelope = exp(-pow((e - 0.5) * 3.5, 2.0));
        float amount = intensity * envelope;
        vec2 shake = vec2(wobble(linear, 1.3), wobble(linear, 4.1)) * 0.06 * amount;
        float angle = wobble(linear, 7.7) * 0.06 * amount;

        vec2 p = toCentered(uv);
        float r2 = dot(p, p);
        p *= 1.0 + 0.9 * amount * r2;                          // lente (barril)
        p.x += sin(p.y * 18.0 + linear * 40.0) * 0.015 * amount; // ondas
        vec2 distorted = fromCentered(p);

        int layer = firstHalf ? 0 : 1;
        vec2 dir = normalize(shake + vec2(1e-5)) * motion * 0.08 * envelope;
        float split = 0.012 * amount; // aberración cromática
        vec4 r = transformed(layer, distorted + vec2(split, 0.0), 1.0, angle, shake, 0.0, 0.0, dir);
        vec4 g = transformed(layer, distorted, 1.0, angle, shake, 0.0, 0.0, dir);
        vec4 b = transformed(layer, distorted - vec2(split, 0.0), 1.0, angle, shake, 0.0, 0.0, dir);
        fragColor = vec4(r.r, g.g, b.b, 1.0);
    } else if (mode == ZOOM_IN || mode == ZOOM_OUT || mode == ZOOM_ROTATE_OUT) {
        float maxZoom = 1.0 + 3.0 * intensity; // con intensidad 0,6 → ×2,8
        float zoomBlur = motion * 0.7;
        bool zoomIn = mode == ZOOM_IN;
        float maxAngle = mode == ZOOM_ROTATE_OUT ? intensity * PI * 0.5 : 0.0;
        if (firstHalf) {
            // A: se acerca (Zoom In) o se aleja (Zoom Out) hasta el corte.
            float scale = zoomIn ? 1.0 + (maxZoom - 1.0) * tA : 1.0 / (1.0 + (maxZoom - 1.0) * tA);
            fragColor = transformed(0, uv, scale, tA * maxAngle, vec2(0.0), zoomBlur,
                                    motion * 0.5 * sign(maxAngle), vec2(0.0));
        } else {
            // B: continúa el mismo movimiento y se asienta a tamaño normal.
            float scale = zoomIn ? 1.0 / (1.0 + (maxZoom - 1.0) * (1.0 - tB))
                                 : 1.0 + (maxZoom - 1.0) * (1.0 - tB);
            fragColor = transformed(1, uv, scale, -(1.0 - tB) * maxAngle, vec2(0.0), zoomBlur,
                                    motion * 0.5 * sign(maxAngle), vec2(0.0));
        }
    } else {
        fragColor = sampleLayer(0, uv);
    }
    fragColor.a = 1.0;
}
