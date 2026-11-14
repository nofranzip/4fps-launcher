# Fondos por temporada y eventos

El launcher cambia el fondo de la parte central según la fecha (hora de Chile):

| Tema | Fechas |
|---|---|
| Año Nuevo | 27 dic – 3 ene |
| Navidad | 1 – 26 dic |
| Halloween | octubre |
| Fiestas Patrias | 10 – 20 sep |
| Verano | 4 ene – 20 mar |
| Normal (gris) | el resto del año |

Prioridad: imagen elegida por el jugador > evento > temporada > gris.
Se apaga en **Ajustes → Funciones 4fps → "Fondos por temporada y eventos"**.
En el mismo lugar, "Fondo de temporada" sirve para ver cada tema sin esperar la fecha.

## Fondos de eventos (Permaquistán, Panaquistán...)

Edita `fondos.json` en la raíz del repo. Los launchers lo leen al abrir:

```json
{
  "eventos": [
    {
      "nombre": "Permaquistán",
      "desde": "2026-12-05",
      "hasta": "2026-12-07",
      "imagen": "https://raw.githubusercontent.com/nofranzip/4fps-launcher/main/fondos/permaquistan.png",
      "color": "#f59e0b",
      "oscurecer": 60
    }
  ]
}
```

- `desde` / `hasta`: fechas AAAA-MM-DD (ambas incluidas).
- `imagen`: opcional; solo se aceptan enlaces de GitHub. Sin imagen, se usa un fondo con el `color`.
- `oscurecer`: 0–90, cuánto se oscurece la imagen para que el texto se lea bien.
