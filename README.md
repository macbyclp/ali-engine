# ali-engine

C++17 3D oyun motoru. Tek kod tabanı → **native** (Windows/Linux/macOS, OpenGL 3.3)
ve **web** (WebAssembly + WebGL2, Emscripten).

## Şu an ne var
- GLFW pencere + GL context (native: glad, web: Emscripten)
- Sabit-adımlı oyun döngüsü (60 Hz update, serbest render) — `src/engine/app.cpp`
- Shader / Mesh / Camera soyutlamaları
- AABB çarpışma + penetrasyon çözümü — `src/engine/collision.hpp`
- **Oynanabilir prototip** (`src/game/game.cpp`): zemin + kutular, WASD + fare bak,
  yerçekimi, zıplama (Space), duvar boyunca kayma. ESC fareyi bırakır/yakalar.

## Bağımlılıklar
CMake FetchContent otomatik çeker: GLM, GLFW 3.4, glad. Elle kurulum yok.

## Kurulum (Windows, ilk kez)
```
winget install Kitware.CMake Microsoft.VisualStudio.2022.BuildTools Ninja-build.Ninja
```
Build Tools kurulumunda **"Desktop development with C++"** işaretle.

## Native build
```
cmake -B build -G Ninja
cmake --build build
./build/game
```
(Ninja yoksa `-G Ninja` yerine varsayılan Visual Studio generator kullanılır:
`cmake -B build` → `cmake --build build --config Debug`, çıktı `build/Debug/game.exe`.)

## Web build (sonra)
```
cmake -B build-web -G Ninja -DCMAKE_TOOLCHAIN_FILE=$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake
cmake --build build-web
python -m http.server -d build-web
# tarayıcıda http://localhost:8000/game.html
```

## Sıradaki adımlar
- glTF model yükleme (cgltf)
- Doku + malzeme sistemi
- Gerçek fizik (Jolt) — AABB'yi devralır
- Sahne serialization
- Editör (Dear ImGui)
