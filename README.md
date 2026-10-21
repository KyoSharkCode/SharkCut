# SharkCut

Editor de video nativo de escritorio para Windows (C++20 + Qt 6), de uso
personal, pensado para el flujo de trabajo de KyoSumiVT: VODs de Twitch,
clips, shorts, anuncios y contenido de stream.

- Plan del proyecto: [docs/PLAN.md](docs/PLAN.md)
- Instrucciones para sesiones de desarrollo: [CLAUDE.md](CLAUDE.md)

Estado:

- Fase 0 (cimientos) ✅: reproducción por GPU (D3D11VA/NVDEC) con audio sincronizado.
- Fase 1 (transiciones) ✅: las 7 transiciones de prioridad en la GPU, iguales a las de
  Premiere Composer, y exportación a MP4 con NVENC.
- Fase 2 (editor básico) ✅: biblioteca de medios, línea de tiempo con cortar, recortar,
  mover y deshacer, proyectos `.sharkp` y exportación de la línea de tiempo.
- Fase 3 (interfaz definitiva y montaje real), en curso: etapa 1 (interfaz nueva con
  lienzos 16:9, 9:16 y 1:1, miniaturas, medidor de la mezcla) ✅; siguen varias pistas
  con reencuadre y onda del audio (etapa 2) y transiciones en la línea de tiempo (etapa 3).

Preparar el PC: [docs/SETUP-WINDOWS.md](docs/SETUP-WINDOWS.md)

## Licencia

SharkCut es software libre y gratuito bajo la licencia GPL 3.0 (ver `LICENSE`). Las
licencias de las bibliotecas que usa están en `licenses/`.
