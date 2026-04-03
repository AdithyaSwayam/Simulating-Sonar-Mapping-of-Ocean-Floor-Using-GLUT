# Simulating-Sonar-Mapping-of-Ocean-Floor-Using-GLUT

An intermediate level project simulating sonar technology to map uneven ocean floor terrain in real time, revealing peaks, ridges, and ravines with depth-based color grading — built using OpenGL and FreeGLUT in C++98.

---

# Sonar Terrain Mapper

A real-time 3D sonar terrain scanner built in C++98 with OpenGL and FreeGLUT. An ROV hovers above a procedurally generated seabed — hold the left mouse button to emit sonar pings and progressively reveal the underwater terrain below. Terrain detail builds up over time: structural geometry appears first, followed by full depth-color mapping after sustained scanning.

---

# Requirements

| Requirement | Detail |
|---|---|
| OS | Windows (primary). Linux buildable with minor library swaps. |
| IDE | Code::Blocks with MinGW (open the `.cbp` to build instantly) |
| RAM | ~400 MB free (1024×1024 terrain grid, ~32 MB of float arrays) |
| GPU | Any OpenGL 1.x capable card |
| C++ Standard | C++98 compatible |

---

# Libraries

| Library | Purpose |
|---|---|
| `freeglut` | Window creation, input handling, mouse wheel support |
| `opengl32` | Core OpenGL rendering |
| `glu32` | Camera setup (`gluLookAt`, `gluPerspective`), quadric shapes |
| `winmm` | Windows multimedia timer |
| `gdi32` | Required by GLUT on Windows |

> **Note:** Must use **FreeGLUT** specifically — classic `glut32` will not work, as the project uses `glutMouseWheelFunc` which is a FreeGLUT extension.

---

# How It Works

The terrain is a 1024×1024 grid generated using layered Perlin noise (fBm) with ridge masks, hill formations, trench cuts, and micro-detail ripples — all computed at startup. Normals are derived analytically from the height field for smooth shading.

The sonar reveal system has two stages per cell:

- **Structural reveal** — geometry lifts from the seabed floor after brief dwell time under the sonar cone
- **Color reveal** — full depth-color mapping unlocks after sustained scanning of the same area

Depth coloring uses a three-band HSL ramp: deep trenches render in cool purples/blues, mid-range terrain in cyan/green, and peaks in yellow/green — thresholds auto-calibrated from the terrain's height histogram.

---

# Building

### Code::Blocks (Recommended)

```
1. Open main.cpp directly in Code::Blocks
2. Configure the linker settings with the required libraries (see Libraries table above)
3. Hit Build and Run (F9)
```

> **Note:** A `.cbp` project file is included in the repository but is hidden by the Code::Blocks interface — most users won't see it and can ignore it entirely. If you want to use it, you can open it manually via `File > Open` or run it from the command line. Otherwise, opening `main.cpp` directly and building works fine.

### Manual MinGW (Windows)

```bash
g++ main.cpp -o "Sonar Terrain Mapper.exe" ^
  -I"C:/Program Files/CodeBlocks/MinGW/include" ^
  -L"C:/Program Files/CodeBlocks/MinGW/lib" ^
  -lfreeglut -lopengl32 -lglu32 -lwinmm -lgdi32
```

### Linux

```bash
g++ main.cpp -o sonar_terrain_mapper -lGL -lGLU -lfreeglut -lm
```

> On Linux, replace `<GL/freeglut.h>` with the system path if needed, and drop the `winmm` / `gdi32` flags.

---

# Controls

| Key / Input | Action |
|---|---|
| `W` / `↑` | Move ROV forward |
| `S` / `↓` | Move ROV backward |
| `A` / `←` | Move ROV left |
| `D` / `→` | Move ROV right |
| Hold **Left Mouse Button** | Emit sonar ping — scans and reveals terrain |
| **Right Mouse Button** drag | Rotate camera around the ROV |
| **Mouse Wheel** | Zoom in / out |
| `+` / `=` | Increase scan speed (up to 10×) |
| `-` / `_` | Decrease scan speed (down to 0.1×) |
| `F` | Toggle full terrain reveal (instant map of entire seabed) |
| `R` | Reset — clears all revealed terrain |
| `Esc` | Exit |

---

# HUD Display

The on-screen overlay shows:

- **ROV Position** — X/Z world coordinates and altitude
- **Terrain Mapped %** — percentage of cells with structural geometry revealed
- **Detailed %** — percentage of cells with full color depth-mapping revealed
- **Grid size** — current terrain resolution (1024×1024 default)
- **Scan Speed** — current multiplier
- **Active mode** — scanning indicator or full-reveal status

---

# Performance Notes

- The terrain uses a **level-of-detail** rendering system — near rows render at full 1-cell resolution, mid-range at 2-cell steps, and far terrain at 4-cell steps, keeping frame rates stable across the full 700×700 world unit grid.
- Sonar update is spatially bounded — only cells within the beam cone are iterated each frame.
- Startup terrain generation and normal computation takes a few seconds on first load due to the 1024×1024 grid size.

---

# Project Structure

```
.
├── main.cpp          # Single-file implementation (terrain gen, sonar sim, rendering, input)
├── README.md
└── Sonar Terrain Mapper.cbp   # Code::Blocks project file (optional)
```
