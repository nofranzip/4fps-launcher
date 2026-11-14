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
- Color del launcher con un círculo cromático e imagen de fondo en la parte central.
- Modo música: muestra la canción que suena (arriba a la izquierda) y la letra si existe.
- Todo se activa/desactiva en Ajustes → Funciones 4fps.

## Novedades 1.4

- **Cliente PvP propio (4fps Client)**: el launcher ahora instala el mod **4fps Client** (editor de HUD con Shift derecho, crosshair, hitboxes y módulos de FPS, CPS, coordenadas, ping y teclas) en vez de OneConfig/PolyHitbox/PolyCrosshair. Instala el jar que corresponde a la versión de Minecraft de cada instancia (por ahora 26.2 Fabric; se irán agregando más). Aparece en la lista de Mods.

## Novedades 1.3

- **RAM automática**: cada instancia calcula su RAM según la RAM del PC y cuántos mods tiene (vanilla 2–3 GB, pocos mods 3 GB, modpack mediano 4–6 GB, grande 6–8 GB), siempre deja 3–4 GB libres para Windows y nunca pasa de 10 GB. Se puede apagar por instancia para elegirla a mano.
- **Fondos por temporada**: Halloween, Navidad, Año Nuevo, verano y Fiestas Patrias según la fecha; fondos de eventos definidos en [`fondos.json`](fondos.json) ([cómo editarlo](packaging-4fps/FONDOS.md)). Se apaga en Ajustes → Funciones 4fps.
- **Premios por medallas**: capas y accesorios que se desbloquean al ganar medallas, definidos en [`premios.json`](premios.json) ([cómo editarlo](packaging-4fps/PREMIOS.md)). Lo bloqueado se ve con candado y el requisito.
- **Modo ligero** para PCs de 8 GB de RAM o menos: sin modo música, menos capas a la vez y solo colores predeterminados.
- El círculo cromático ya no traba el launcher.
- **Ver capas de otros jugadores**: instala MinecraftCapes en las instancias Fabric (interruptor en la instancia, encendido por defecto) para ver —y que vean— las capas de otros que usen 4fps. Aparece en la lista de Mods. Funciona en Fabric, Forge y NeoForge.

## Compilar
Ver `packaging-4fps/COMPILAR.md`. El pack propio de ropa y capas se genera con `packaging-4fps/generar_pack4fps.py`.

## Licencia
GPL-3.0, igual que Prism Launcher. Ver `LICENSE` y `COPYING.md`.
