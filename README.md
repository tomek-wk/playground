# playground

Pierwszy eksperyment: prosta voxelowa aplikacja C++ / OpenGL 4.3 Core / Dear ImGui.

Aktualny baseline:
- exclusive fullscreen na glownym monitorze;
- ESC zamyka program;
- W/S/A/D + mysz, free flight;
- swiat 100x100x100, voxel 1 m;
- trzy losowe kolory blokow;
- pelne kostki renderowane instancingiem, bez meshingu/optimizacji;
- celowanie ze srodka ekranu, zasieg 5 m;
- LMB usuwa jeden blok na klik;
- RMB wstawia jeden blok przy trafionej scianie na klik;
- scroll wybiera czerwony/zielony/niebieski blok;
- prosty pasek wyboru na dole i celownik przez Dear ImGui.

Build (Ninja):

```text
cmake -S . -B build -G Ninja
cmake --build build
```

Przy pierwszej konfiguracji CMake pobiera GLFW, GLM, GLEW i Dear ImGui przez FetchContent.
