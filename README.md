# MyEngine

A C++20 game engine with a Unity-style editor, real-time 3D rendering, physics, audio, and native **OpenXR VR** support.

<img width="1912" height="1029" alt="Editor screenshot" src="https://github.com/user-attachments/assets/2ef60a42-f32e-4de1-b004-42864845ea32" />

---

## Features

### Core
- Entity-component scene graph with parent/child relationships and UUIDs
- YAML scene serialization (`.scene` files) and project files (`.myproject`)
- Hot-reloadable native C++ scripts via DLL (`GameModule`)
- Built-in `Editor` and `GameRuntime` build targets — the runtime is a standalone executable with no editor code

### Rendering
- Deferred-style OpenGL 4.6 pipeline (GLEW + SDL2)
- PBR materials (metallic / roughness / AO), normal mapping
- Shadow-mapped directional, point, spot, and area lights
- Post-process stack: bloom, SSAO, SSR, volumetric fog, god rays, lens flare, DoF, motion blur, TAA, chromatic aberration, film grain, color grading, auto-exposure
- Mesh LODs, planar reflections, Gerstner-wave water with reflection/refraction
- Particle systems, skeletal animation, 2D sprites + text rendering
- Editor grid, ImGuizmo gizmos for translate/rotate/scale

### Physics & Audio
- Bullet Physics 3 for rigid bodies, box/sphere/capsule/mesh colliders
- Built-in audio engine with 3D spatialization

### Editor
- Dockable ImGui panels: Scene Hierarchy, Inspector, Content Browser, Rendering, Viewport
- Marquee selection, multi-select transforms, undo/redo history
- Per-project recent-scene list, autosave, in-editor play mode
- Standalone game build (copies `GameRuntime.exe` + assets + `Game.dll`)

### VR (OpenXR)
- Stereoscopic per-eye rendering bound to an `XrSwapchain`
- Action-based input: grip/aim pose, trigger, squeeze, thumbstick, A/B/X/Y buttons (Touch + Index profiles)
- `VRRigComponent` for declarative rig setup in the Inspector
- `VRRigSystem` drives HMD position, smooth/snap locomotion, and child-entity hand poses
- Preview Mode: simulate the HMD with mouse + WASD when no headset is attached
- Graceful fallback to flat desktop rendering if no OpenXR runtime is registered

---

## Tech Stack

| | |
|--|--|
| Language | C++20 |
| Windowing / Input | SDL2 |
| Graphics | OpenGL 4.6 (GLEW) |
| Math | GLM |
| GUI | Dear ImGui (docking) + ImGuizmo |
| Physics | Bullet Physics 3 |
| Serialization | yaml-cpp |
| VR | OpenXR SDK 1.1.41 |
| Mesh loading | tinyobjloader, cgltf |
| Tests | GoogleTest |

All dependencies are fetched and built automatically via CMake `FetchContent`.

---

## Build

### Requirements
- Visual Studio 2022 (Desktop development with C++)
- CMake 3.20+
- Windows 10/11

### Configure & build
```bash
cmake -S . -B build
cmake --build build --config Release
```

The first configure takes a few minutes — it downloads SDL2, Bullet, ImGui, GLEW, OpenXR, yaml-cpp, GoogleTest, and others.

### Run
```bash
./build/bin/Release/Engine.exe        # editor
./build/bin/Release/GameRuntime.exe   # standalone runtime
```

---

## Editor controls

| Key | Action |
|--|--|
| Right click + WASD | Fly camera |
| Q / W / E / R | Gizmo: None / Translate / Rotate / Scale |
| Ctrl+S | Save scene |
| Ctrl+O | Open scene |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Esc (in Play mode) | Exit play mode |

---

## VR usage

1. Install an OpenXR runtime (Meta Quest Link, SteamVR, or WMR) and set it as **default** in its settings.
2. Open or create a project and load `assets/VR_Demo.scene` for a starter scene.
3. The scene includes a `Player` entity with `VRRigComponent`. Open **Rendering → VR (OpenXR)** to verify *Headset ACTIVE*.
4. Press **Play**. The scene renders stereoscopically; locomotion comes from the left thumbstick, snap turn from the right.

### Without a headset (Preview Mode)
On the `VRRigComponent`, toggle **Preview Mode**. In Play mode, right-click + WASD simulates the HMD; E/Q raise/lower the head.

### Authoring a VR rig from scratch
1. Create an entity → **Add Component → VR Rig**.
2. Optionally add child entities named `LeftHand` and `RightHand` with mesh renderers — `VRRigSystem` will drive their transforms from the controller grip poses.
3. The rig entity needs a `CameraComponent` with `Primary = true` so Preview Mode and non-VR fallback render through it.

---

## Project layout

```
src/
  Engine/        # engine library: rendering, ECS, physics, VR, editor panels
    Animation/   # skeletal animation
    Audio/       # audio engine
    Core/        # Application, Input, UUID
    Editor/      # ImGui editor panels
    Project/     # project / asset / script-build management
    Renderer/    # OpenGL backend, materials, shaders, meshes
    Scene/       # ECS scene graph, serialization
    Scripting/   # native script binding + hot-reload
    Utils/       # platform helpers
    VR/          # OpenXR session, swapchains, action system, VRRigSystem
    Window/      # SDL2 window + GL context
    Scripts/     # built-in ScriptableEntity examples
  Scripts/       # gameplay scripts
  main.cpp       # editor + runtime entry point
assets/          # game assets (scenes, scripts, fonts)
resources/       # editor icons
cmake/           # build patches (ImGuizmo signature fix)
tests/           # GoogleTest unit tests
```

---

## License

MIT — see `LICENSE`.
