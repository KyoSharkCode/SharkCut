#!/usr/bin/env python3
"""Genera los efectos de sonido de SharkCut (presets/sfx/*.wav).

Todos se sintetizan aquí, desde cero (ruido filtrado, barridos y envolventes): son
propios y se publican como CC0. No se usa ninguna grabación ni muestra de terceros.

Uso: python3 tools/sfx/generar_sfx.py [carpeta]   (por defecto, presets/sfx)
Requiere numpy. El resultado es siempre el mismo (semillas fijas).

El "golpe" de cada sonido (el instante que cae en el corte de la transición) está en
src/core/sfx.h; si se cambia aquí la forma de un sonido, hay que actualizarlo allí.
"""

import math
import pathlib
import sys
import wave

import numpy as np

SR = 48_000


def tiempo(segundos):
    return np.arange(int(round(segundos * SR))) / SR


def ruido(n, semilla, rosa=True):
    """Ruido blanco o (aprox.) rosa, normalizado."""
    r = np.random.default_rng(semilla).standard_normal(n)
    if rosa:
        # Filtro de Paul Kellet (versión económica): ruido rosa suficiente para whooshes.
        b0 = b1 = b2 = 0.0
        out = np.empty(n)
        for i, w in enumerate(r):
            b0 = 0.99765 * b0 + w * 0.0990460
            b1 = 0.96300 * b1 + w * 0.2965164
            b2 = 0.57000 * b2 + w * 1.0526913
            out[i] = b0 + b1 + b2 + w * 0.1848
        r = out
    return r / np.max(np.abs(r))


def pasabanda(x, centro, q):
    """Filtro paso banda (biquad RBJ) con frecuencia central variable en el tiempo."""
    y = np.zeros_like(x)
    x1 = x2 = y1 = y2 = 0.0
    for i in range(len(x)):
        w0 = 2 * math.pi * min(centro[i], SR * 0.45) / SR
        alfa = math.sin(w0) / (2 * q)
        a0 = 1 + alfa
        b0 = alfa / a0
        b2 = -alfa / a0
        a1 = -2 * math.cos(w0) / a0
        a2 = (1 - alfa) / a0
        v = b0 * x[i] + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, x[i]
        y2, y1 = y1, v
        y[i] = v
    return y


def pasabajo(x, corte):
    """Paso bajo de un polo (suaviza ruidos y golpes)."""
    a = math.exp(-2 * math.pi * corte / SR)
    y = np.zeros_like(x)
    prev = 0.0
    for i, v in enumerate(x):
        prev = (1 - a) * v + a * prev
        y[i] = prev
    return y


def joroba(t, pico, subida=2.0, caida=4.0):
    """Envolvente que sube hasta `pico` y cae después (0–1)."""
    duracion = t[-1]
    env = np.where(
        t < pico,
        (t / pico) ** subida,
        np.exp(-caida * (t - pico) / max(duracion - pico, 1e-3)),
    )
    return env


def barrido(t, inicio, medio, fin, pico):
    """Frecuencia que va de `inicio` a `medio` (en `pico`) y de ahí a `fin`, exponencial."""
    duracion = t[-1]
    return np.where(
        t < pico,
        inicio * (medio / inicio) ** (t / pico),
        medio * (fin / medio) ** ((t - pico) / max(duracion - pico, 1e-3)),
    )


def paneo(mono, desde, hasta):
    """Estéreo con paneo de potencia constante (-1 izquierda, 1 derecha)."""
    p = np.linspace(desde, hasta, len(mono))
    angulo = (p + 1) * math.pi / 4
    return np.stack([mono * np.cos(angulo), mono * np.sin(angulo)], axis=1)


def bordes(stereo, entrada=0.004, salida=0.03):
    """Fundidos muy cortos para que no haya chasquidos al principio ni al final."""
    n = len(stereo)
    env = np.ones(n)
    a = int(entrada * SR)
    b = int(salida * SR)
    env[:a] = np.linspace(0, 1, a)
    env[n - b :] = np.linspace(1, 0, b)
    return stereo * env[:, None]


def guardar(carpeta, nombre, stereo, pico_db=-3.0):
    stereo = bordes(stereo)
    stereo = stereo / np.max(np.abs(stereo)) * 10 ** (pico_db / 20)
    datos = (np.clip(stereo, -1, 1) * 32767).astype("<i2")
    ruta = carpeta / nombre
    with wave.open(str(ruta), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(datos.tobytes())
    print(f"{ruta}: {len(datos) / SR:.3f} s")


def whoosh(semilla, duracion, pico, f0, f1, f2, q, desde, hasta, subida=2.0, caida=4.0):
    t = tiempo(duracion)
    señal = pasabanda(ruido(len(t), semilla), barrido(t, f0, f1, f2, pico), q)
    # Una segunda banda, más grave y ancha, da cuerpo al aire que pasa.
    cuerpo = pasabanda(ruido(len(t), semilla + 1), barrido(t, f0 * 0.5, f1 * 0.4, f2 * 0.5, pico), 0.7)
    mono = (señal + 0.6 * cuerpo) * joroba(t, pico, subida, caida)
    return paneo(mono, desde, hasta)


def impacto(semilla):
    t = tiempo(1.0)
    # Bombo: seno que cae de 120 a 38 Hz con caída larga.
    frecuencia = 38 + 82 * np.exp(-t * 18)
    fase = 2 * math.pi * np.cumsum(frecuencia) / SR
    bombo = np.sin(fase) * np.exp(-t * 4.5)
    # Chasquido de ataque y "polvo" que se apaga.
    golpe = pasabajo(ruido(len(t), semilla, rosa=False), 2500) * np.exp(-t * 60)
    cola = pasabanda(ruido(len(t), semilla + 1), np.full(len(t), 900.0), 0.8) * np.exp(-t * 9)
    mono = np.tanh(1.6 * (bombo + 0.9 * golpe + 0.35 * cola))
    estereo = paneo(mono, 0, 0)
    # Algo de anchura: la cola un poco desplazada en cada canal.
    retardo = int(0.011 * SR)
    estereo[retardo:, 1] += 0.25 * cola[:-retardo] * np.exp(-t[retardo:] * 9)
    return estereo


def glitch(semilla):
    t = tiempo(0.5)
    rng = np.random.default_rng(semilla)
    mono = np.zeros(len(t))
    i = 0
    while i < len(t):
        largo = int(rng.uniform(0.012, 0.045) * SR)
        tramo = slice(i, min(i + largo, len(t)))
        n = tramo.stop - tramo.start
        tipo = rng.integers(0, 3)
        if tipo == 0:  # tono cuadrado "digital"
            f = rng.uniform(300, 2400)
            mono[tramo] = np.sign(np.sin(2 * math.pi * f * np.arange(n) / SR))
        elif tipo == 1:  # ruido con muestreo reducido (bitcrush)
            paso = int(rng.integers(6, 30))
            r = rng.uniform(-1, 1, n // paso + 1)
            mono[tramo] = np.repeat(r, paso)[:n]
        else:  # silencio entrecortado
            mono[tramo] = 0
        i += largo
    mono *= joroba(t, 0.18, 1.0, 3.0)
    mono = np.round(mono * 6) / 6  # menos resolución: más "roto"
    return paneo(mono, -0.3, 0.3)


def riser(semilla):
    t = tiempo(1.2)
    f = 180 * (1400 / 180) ** (t / t[-1])
    tono = np.sin(2 * math.pi * np.cumsum(f) / SR) * 0.35
    aire = pasabanda(ruido(len(t), semilla), f * 2.5, 1.5)
    mono = (tono + aire) * (t / t[-1]) ** 2.2
    return paneo(mono, -0.4, 0.4)


def clic(semilla, suelta=True):
    """Clic de ratón: golpe seco de plástico al pulsar y, si `suelta`, otro más flojo al soltar."""
    duracion = 0.16 if suelta else 0.08
    t = tiempo(duracion)
    mono = np.zeros(len(t))

    def golpe(inicio, fuerza, tono):
        n = len(t) - int(inicio * SR)
        tt = np.arange(n) / SR
        chasquido = pasabajo(ruido(n, semilla, rosa=False), 7000) * np.exp(-tt * 900)
        resonancia = np.sin(2 * math.pi * tono * tt) * np.exp(-tt * 260)
        mono[int(inicio * SR):] += fuerza * (chasquido + 0.5 * resonancia)

    golpe(0.0, 1.0, 3400)
    if suelta:
        golpe(0.075, 0.55, 2900)
    return paneo(mono, 0, 0)


def pop(semilla, f_inicio, f_fin, duracion, caida, hueco):
    """Pop: tono que cambia rápido de frecuencia con caída corta; `hueco` añade la
    resonancia grave de una boca o un tubo."""
    t = tiempo(duracion)
    f = f_fin + (f_inicio - f_fin) * np.exp(-t * 55)
    tono = np.sin(2 * math.pi * np.cumsum(f) / SR) * np.exp(-t * caida)
    ataque = pasabajo(ruido(len(t), semilla, rosa=False), 4000) * np.exp(-t * 500) * 0.4
    cuerpo = pasabanda(ruido(len(t), semilla + 1), np.full(len(t), 420.0), 4.0) * np.exp(-t * caida * 0.8)
    mono = np.tanh(1.4 * (tono + ataque + hueco * 1.5 * cuerpo))
    return paneo(mono, 0, 0)


def main():
    carpeta = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "presets/sfx")
    carpeta.mkdir(parents=True, exist_ok=True)
    # Golpe (en src/core/sfx.h): whoosh 0,40 s · subida 0,55 · bajada 0,30 · swoosh 0,22 ·
    # giro 0,40 · impacto 0,00 · glitch 0,18 · riser 1,20.
    guardar(carpeta, "whoosh.wav", whoosh(11, 0.75, 0.40, 300, 2600, 700, 1.3, -0.6, 0.6))
    guardar(carpeta, "whoosh-subida.wav", whoosh(12, 0.65, 0.55, 220, 6000, 4000, 1.6, -0.2, 0.2, 2.6, 6.0))
    guardar(carpeta, "whoosh-bajada.wav", whoosh(13, 0.75, 0.30, 5200, 1800, 160, 1.4, 0.2, -0.2, 1.4, 3.0))
    guardar(carpeta, "swoosh.wav", whoosh(14, 0.45, 0.22, 450, 3600, 900, 2.2, 0.8, -0.8, 2.0, 5.0))
    t = tiempo(0.85)
    giro = whoosh(15, 0.85, 0.40, 350, 2200, 500, 1.4, -0.3, 0.3)
    temblor = 0.55 + 0.45 * np.sin(2 * math.pi * np.cumsum(16 - 10 * t / t[-1]) / SR)
    guardar(carpeta, "giro.wav", giro * temblor[:, None])
    guardar(carpeta, "impacto.wav", impacto(16), pico_db=-1.0)
    guardar(carpeta, "glitch.wav", glitch(17), pico_db=-6.0)
    guardar(carpeta, "riser.wav", riser(18))
    # Clics, pops y más swooshes (golpe: clics y pops 0,00 · swoosh-corto 0,14 ·
    # swoosh-grave 0,18).
    guardar(carpeta, "clic-raton.wav", clic(19), pico_db=-4.0)
    guardar(carpeta, "clic-suave.wav", clic(20, suelta=False), pico_db=-6.0)
    guardar(carpeta, "pop-hueco.wav", pop(21, 950, 330, 0.22, 26, 1.0), pico_db=-3.0)
    guardar(carpeta, "pop-burbuja.wav", pop(22, 380, 1250, 0.16, 34, 0.2), pico_db=-4.0)
    guardar(carpeta, "swoosh-corto.wav", whoosh(23, 0.30, 0.14, 700, 5200, 1500, 2.6, -0.8, 0.8, 1.8, 6.0))
    guardar(carpeta, "swoosh-grave.wav", whoosh(24, 0.40, 0.18, 260, 2400, 500, 1.8, 0.6, -0.6, 1.8, 5.0))


if __name__ == "__main__":
    main()
