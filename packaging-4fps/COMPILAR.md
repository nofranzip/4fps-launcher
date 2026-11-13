# Compilar 4fps

4fps está basado en [Prism Launcher](https://github.com/PrismLauncher/PrismLauncher) (GPL-3.0).

## Windows (cross-compilado desde Linux con MinGW)

1. Qt 6.8 para Windows (qtbase, qtsvg, qtimageformats, qtnetworkauth) compilado con `mingw-toolchain.cmake`.
2. Dependencias: zlib, cmark, libqrencode, libarchive, tomlplusplus, extra-cmake-modules.
3. Configurar:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=packaging-4fps/mingw-toolchain.cmake \
     -DQT_HOST_PATH=/opt/qthost -DCMAKE_PREFIX_PATH="/opt/qt-win;/opt/mingw-deps" \
     -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DLauncher_USE_PCH=OFF
   cmake --build build
   ```
4. Copiar `4fps.exe`, las DLL de Qt/MinGW y los plugins a una carpeta y ejecutar
   `makensis win_install_4fps.nsi` dentro de ella para generar `4fps-Setup.exe`.

## Archivos propios de 4fps

- `launcher/ui/fourfps/` — interfaz nueva (FourFpsShell, FourFpsPages) y render de skin (SkinRender)
- `launcher/ui/themes/FourFpsStyle.inc` — tema
- `program_info/` — logo e íconos 4fps
