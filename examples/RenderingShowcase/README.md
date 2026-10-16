# Rendering showcase

A self-contained material gallery made from procedural cubes, spheres and a ring.
The scene and meshes are public engine examples; no external game project is
required. Open `Showcase.myproject` in the editor, or run:

```powershell
.\build\bin\Release\Engine.exe .\examples\RenderingShowcase\Showcase.myproject
```

The three spheres vary in metallic/roughness settings. Directional shadows and
colored fill lights reveal their surface response. `Assets/Scripts/Showcase.cpp`
shows how a project opts in to the separate stereo rendering profile.

To regenerate the scene and geometry, use Python 3 (standard library only):

```powershell
python examples/RenderingShowcase/tools/generate.py
```

Generated assets are included, so Python is optional when opening the example.
This is a rendering example, not a complete VR interaction or locomotion demo.
