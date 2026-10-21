#!/usr/bin/env python3
"""Genera el icono de SharkCut (src/app/icons/sharkcut.svg y sharkcut.ico).

Es el mismo logo que dibuja la app (qml/Trazos.qml, "aleta"): la aleta en Bioluz sobre un
cuadro redondeado azul abisal con borde Aleta (colores de la marca Marea Nocturna, ver
src/app/qml/Theme.qml). El .ico lleva los tamaños de Windows (16 a 256 px).

Uso: python3 tools/icono/generar_icono.py   (necesita Pillow)
"""

import pathlib

from PIL import Image, ImageDraw

RAIZ = pathlib.Path(__file__).resolve().parents[2]
DESTINO = RAIZ / "src" / "app" / "icons"

SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <rect x="8" y="8" width="240" height="240" rx="56" fill="#0A1030" stroke="#3A7BD5" stroke-width="10"/>
  <g transform="translate(40 36) scale(7.3)" fill="none" stroke="#6FE3F0" stroke-width="2.1"
     stroke-linecap="round" stroke-linejoin="round">
    <path d="M3 18c4-1 7-6 9-14 1 6 4 11 9 14M3 18h18"/>
  </g>
</svg>
"""


def bezier(p0, p1, p2, p3, pasos=64):
    """Puntos de una curva cúbica."""
    puntos = []
    for i in range(pasos + 1):
        t = i / pasos
        u = 1 - t
        puntos.append(tuple(u**3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t**3 * d
                            for a, b, c, d in zip(p0, p1, p2, p3)))
    return puntos


def dibujar(lado=1024):
    """El icono a `lado` px (se dibuja grande y se reduce: bordes suaves)."""
    k = lado / 256
    imagen = Image.new("RGBA", (lado, lado), (0, 0, 0, 0))
    d = ImageDraw.Draw(imagen)
    d.rounded_rectangle([8 * k, 8 * k, 248 * k, 248 * k], radius=56 * k, fill="#0A1030",
                        outline="#3A7BD5", width=round(10 * k))
    # La aleta (rejilla de 24 × 24 de Trazos.qml) escalada 7,3 veces desde (40, 36).
    def a_px(x, y):
        return ((40 + 7.3 * x) * k, (36 + 7.3 * y) * k)
    curva = bezier((3, 18), (7, 17), (10, 12), (12, 4), 600) + \
        bezier((12, 4), (13, 10), (16, 15), (21, 18), 600)
    recta = [(3 + 18 * i / 600, 18) for i in range(601)]
    radio = 2.1 * 7.3 * k / 2
    # Un círculo en cada punto: trazo de grosor uniforme y extremos redondos, sin dientes.
    for x, y in (a_px(x, y) for x, y in curva + recta):
        d.ellipse([x - radio, y - radio, x + radio, y + radio], fill="#6FE3F0")
    return imagen.resize((256, 256), Image.LANCZOS)


def main():
    DESTINO.mkdir(parents=True, exist_ok=True)
    (DESTINO / "sharkcut.svg").write_text(SVG, encoding="utf-8")
    imagen = dibujar()
    imagen.save(DESTINO / "sharkcut.png")
    imagen.save(DESTINO / "sharkcut.ico", sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    print(DESTINO / "sharkcut.ico")


if __name__ == "__main__":
    main()
