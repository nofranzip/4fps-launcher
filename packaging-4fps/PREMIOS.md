# Cosméticos desbloqueables por medallas

La lista vive en `premios.json` (raíz del repo) y las imágenes en `premios/`.
El launcher la baja al abrir; si no hay internet usa la copia que trae el instalador.

```json
{
  "premios": [
    {
      "id": "capa-permaquistan",
      "nombre": "Capa Permaquistán",
      "tipo": "capa",
      "archivo": "capa-permaquistan.png",
      "url": "https://raw.githubusercontent.com/nofranzip/4fps-launcher/main/premios/capa-permaquistan.png",
      "requisito": { "medalla": "permaquistan" },
      "descripcion": "Por jugar en Permaquistán"
    }
  ]
}
```

- `tipo`: `capa` (PNG 64×32) o `ropa` (PNG 64×64 en formato skin, capa externa).
- `requisito`: `{ "medalla": "<id>" }`, `{ "medallas": 3 }` (cantidad), o ambos.
- IDs de medallas: `equipo`, `bienvenida`, `permaquistan`, `panaquistan`, `panaquistan2`.
- `id` no se debe cambiar después de publicarlo (se usa para avisar una sola vez).

Lo bloqueado aparece con candado y el requisito. Al ganar la medalla se desbloquea solo
y llega un aviso a la campanita.
