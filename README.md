# 4fps Launcher

Launcher de Minecraft **4fps**, basado en [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) (GPL-3.0).

## Qué trae
- Interfaz propia en tonos grises (Instancias, Versiones, Mods, Archivos, Servers, Skins, Perfil).
- Cliente PvP opcional en instancias Fabric: OneConfig de Polyfrost (menú de mods, editor de HUD) + PolyHitbox.
- Ropa y capas sobre la skin, galería de capas y capas en el juego (solo 4fps) vía Cape Provider.
- Render 3D de la skin con poses, armaduras, objetos y exportación sin fondo.
- Perfil con medallas por servidor o código y notificaciones.
- Logos y jugadores en línea de cada server; banners por versión en las instancias.

## Funciones 1.1
- Detector de conflictos de mods antes de jugar (librerías repetidas en jar-in-jar, `depends`/`breaks` de Fabric).
- Explicador de crashes en español (Mixin, NoSuchMethodError, mods incompatibles, RAM, Java).
- Grupos de mods que se actualizan juntos (y bloqueados) al buscar actualizaciones.
- Estado de mis servidores en la portada (en línea / pendiente de evento, jugadores, ping, versión, MOTD).
- Próximo evento en el Perfil, leído de `eventos.json` (edítalo en este repo).
- Galería de capturas y clips de Flashback.
- Modo stream, Modo potato (rendimiento: mods de optimización, Java y video para PCs débiles), Discord Rich Presence y letras de canciones (SMTC + LRCLIB, ventana flotante y página para OBS en `http://127.0.0.1:47323/`).
- Aviso de versión nueva (compara con la última release).
- Color del launcher a gusto con un círculo cromático (el fondo sigue negro).
- Todo se activa/desactiva en Ajustes → Funciones 4fps.

## Compilar
Ver `packaging-4fps/COMPILAR.md`. El pack propio de ropa y capas se genera con `packaging-4fps/generar_pack4fps.py`.

## Licencia
GPL-3.0, igual que Prism Launcher. Ver `LICENSE` y `COPYING.md`.
