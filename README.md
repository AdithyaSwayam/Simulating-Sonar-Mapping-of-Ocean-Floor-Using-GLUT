# Simulating-Sonar-Mapping-of-Ocean-Floor-Using-GLUT
An Intermediate level project rendering sonar technology in mapping uneven ocean floor terrain with contrasting colors displaying peaks and ravines, using GLUT in C++98. 

# Sonar Mapping [Main]
A real-time 3D sonar terrain scanner built in C++98 with OpenGL and FreeGLUT. An ROV hovers over a procedurally generated seabed — hold the left mouse button to ping and gradually reveal the terrain below.

# Requirements

OS: Windows (primary). Linux buildable with minor lib swaps.
IDE: Code::Blocks with MinGW (open the .cbp to build instantly)
RAM: ~400 MB free (2048×2048 terrain grid)
GPU: Any OpenGL 1.x capable card

# Libraries
LibraryPurposefreeglutWindow, input, mouse wheelopengl32Renderingglu32Camera, quadricswinmmWindows timergdi32Required by GLUT on Windows

Must use FreeGLUT specifically — classic glut32 won't work as the project uses glutMouseWheelFunc.


# Building
Code::Blocks (recommended)

# Open Sonar Mapping [Main].cpp
Hit compile and build to run

# Manual MinGW
bashg++ main.cpp -o "Sonar Mapping [Main].exe" ^
  -I"C:/Program Files/CodeBlocks/MinGW/include" ^
  -L"C:/Program Files/CodeBlocks/MinGW/lib" ^
  -lfreeglut -lopengl32 -lglu32 -lwinmm -lgdi32
  
# Linux
bashg++ main.cpp -o sonar_mapping -lGL -lGLU -lfreeglut -lm

# Controls
Key / Input
Action Move / W A S D 
Arrow Keys / Move ROV
Hold Left Mouse Button / Scans Terrain 
PingMouse Wheel / Zoom Reset
