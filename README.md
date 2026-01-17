# C++ Game Engine

A custom engine made based in Unity, but using C++
<img width="1912" height="1029" alt="Screenshot_1" src="https://github.com/user-attachments/assets/2ef60a42-f32e-4de1-b004-42864845ea32" />

## Features

- **Entity Component System (ECS):** Management of game objects
- **Rendering:**
  - 2D and 3D rendering.
  - Support for Shaders, Textures, and Framebuffers.
  - Orthographic and Perspective Cameras.
- **Physics Engine:** Integrated **Bullet Physics** for 3D collision and rigid body dynamics.
- **Audio System:** Support for sound effects and background music.
- **Visual Editor:**
  - **Scene Hierarchy Panel:** Manage entities and their parent-child relations.
  - **Properties Panel (Inspector):** Real-time editing of component values.
  - **Content Browser:** Navigate and manage project assets.
  - **Gizmos:** Visual manipulation tools (Translate, Rotate, Scale).
- **Scripting:** Native C++ scripting support (e.g., `FollowPlayer`, `MainMenu`).
- **Serialization:** Save and load full scenes using YAML format (need fix).
- **UI System:** Built-in support for debugging.

## Tech Stack & Methods

The engine is built using:

- **Language:** C++17/C++20
- **Windowing & Input:** SDL2
- **Graphics API:** OpenGL (via GLEW)
- **Math:** GLM (OpenGL Mathematics)
- **GUI:** Dear ImGui
- **Physics:** Bullet Physics 3
- **Serialization:** yaml-cpp

## Requirements

Before building the engine, ensure you have the following installed in your system:

- **Compiler:** Visual Studio 2022 (with "Desktop development with C++")
- **CMake:** Version 3.20 or higher (usually included with Visual Studio)

## How to Build and Run

### 1. Clone the Repository
```bash
git clone <repository-url>
cd engine
```

### 2. Configure the Build
Uses CMake to fetch dependencies (SDL2, GLM, Bullet, etc.) automatically.
```bash
cmake -S . -B build
```
*Note: This step may take a few minutes as it downloads and compiles dependencies.*

### 3. Compile the Engine
```bash
cmake --build build --config Release
```

### 4. Run existing Applications
After the build, the executables will be located in `build/bin/Release/`.

- **To run the Editor:**
  ```bash
  ./build/bin/Release/Engine.exe
  ```
- **To run the Game Runtime:**
  ```bash
  ./build/bin/Release/GameRuntime.exe
  ```

## Controls (Editor)

- **Right Click + WASD:** Move Camera
- **Q, W, E, R:** Select Gizmo Operation (None, Translate, Rotate, Scale)
- **Ctrl + S:** Save Scene
- **Ctrl + O:** Open Scene
- **Ctrl + Z / Ctrl + Y:** Undo/Redo

## Project Structure

- `src/Engine`: Core engine source code (Renderer, ECS, Physics, etc.)
- `src/Scripts`: Gameplay scripts.
- `assets/`: Game assets (Images, Shaders, Scenes).

---
*Built with C++*
