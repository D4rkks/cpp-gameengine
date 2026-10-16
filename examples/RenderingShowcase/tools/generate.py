"""Build a public, procedural material gallery using only the Python stdlib."""
from pathlib import Path
import json
import math

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "Assets/Models"
MODELS.mkdir(parents=True, exist_ok=True)
(ROOT / "Assets/Scenes").mkdir(parents=True, exist_ok=True)
(ROOT / "Assets/Scripts").mkdir(parents=True, exist_ok=True)
(ROOT / "Assets/Textures").mkdir(parents=True, exist_ok=True)


def obj(name, vertices, normals, faces):
    lines = ["# Procedural engine showcase geometry; MIT license."]
    lines += ["v %.6f %.6f %.6f" % tuple(v) for v in vertices]
    lines += ["vn %.6f %.6f %.6f" % tuple(v) for v in normals]
    lines += ["f " + " ".join(f"{i+1}//{i+1}" for i in f) for f in faces]
    (MODELS / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


vertices, normals, faces = [], [], []
for normal, quad in [
    ((0, 0, 1), [(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]),
    ((0, 0,-1), [(1,-1,-1),(-1,-1,-1),(-1,1,-1),(1,1,-1)]),
    ((1, 0, 0), [(1,-1,1),(1,-1,-1),(1,1,-1),(1,1,1)]),
    ((-1,0, 0), [(-1,-1,-1),(-1,-1,1),(-1,1,1),(-1,1,-1)]),
    ((0, 1, 0), [(-1,1,1),(1,1,1),(1,1,-1),(-1,1,-1)]),
    ((0,-1, 0), [(-1,-1,-1),(1,-1,-1),(1,-1,1),(-1,-1,1)]),
]:
    base = len(vertices)
    vertices += [tuple(x * .5 for x in v) for v in quad]
    normals += [normal] * 4
    faces += [(base,base+1,base+2),(base,base+2,base+3)]
obj("cube.obj", vertices, normals, faces)

vertices, normals, faces = [], [], []
for y in range(17):
    lat = math.pi * y / 16
    for x in range(33):
        lon = 2 * math.pi * x / 32
        n = (math.sin(lat)*math.cos(lon), math.cos(lat), math.sin(lat)*math.sin(lon))
        vertices.append(tuple(c * .5 for c in n)); normals.append(n)
for y in range(16):
    for x in range(32):
        a = y*33+x; b = a+33
        faces += [(a,a+1,b+1),(a,b+1,b)]
obj("sphere.obj", vertices, normals, faces)

vertices, normals, faces = [], [], []
for i in range(49):
    a = 2*math.pi*i/48
    for j in range(13):
        b = 2*math.pi*j/12
        n = (math.cos(a)*math.cos(b), math.sin(b), math.sin(a)*math.cos(b))
        vertices.append(((.7+.075*math.cos(b))*math.cos(a), .075*math.sin(b), (.7+.075*math.cos(b))*math.sin(a)))
        normals.append(n)
for i in range(48):
    for j in range(12):
        a = i*13+j; b = a+13
        faces += [(a,a+1,b+1),(a,b+1,b)]
obj("ring.obj", vertices, normals, faces)

entities = []


def entity(name, position, scale=(1,1,1), rotation=(0,0,0), **components):
    row = {"Entity":1000+len(entities), "Name":name,
           "TransformComponent":{"Translation":list(position), "Rotation":list(rotation), "Scale":list(scale)}}
    row.update(components); entities.append(row)


def mesh(name, model, position, scale, color, metallic=0, roughness=.7, rotation=(0,0,0), **extra):
    entity(name,position,scale,rotation,MeshRendererComponent={
        "FilePath":"Assets/Models/"+model, "Color":[*color,1], "Metallic":metallic,
        "Roughness":roughness, "AO":1, "CastsShadow":True, "ReceivesShadow":True, **extra})


mesh("Gallery floor","cube.obj",(0,-1.05,0),(7,.15,5),(.17,.20,.27),roughness=.8)
mesh("Backdrop","cube.obj",(0,.35,-2),(7,2.8,.15),(.07,.10,.17))
for i, x in enumerate((-1.65,0,1.65)):
    mesh(f"Pedestal {i+1}","cube.obj",(x,-.77,0),(1.3,.4,1.3),(.38,.43,.52),roughness=.55)
    mesh(f"Trim {i+1}","cube.obj",(x,-.55,0),(1.32,.045,1.32),(.10,.65,.76),roughness=.45)
    mesh(f"Material {i+1}","sphere.obj",(x,.07,0),(1.2,1.2,1.2),
         ((.12,.66,.78),(.96,.48,.12),(.70,.77,.85))[i],metallic=(0,.25,.9)[i],roughness=(.7,.35,.18)[i])
mesh("Orbit ring","ring.obj",(0,.08,0),(1.25,1.25,1.25),(.96,.70,.19),.7,.24,rotation=(.55,0,.25))
for x in (-2.8,2.8):
    mesh("Light column","cube.obj",(x,.3,-1.6),(.12,2.6,.12),(.15,.8,.95))
for i in range(7):
    mesh("Wall inlay","cube.obj",(-2.1+i*.7,1.2,-1.9),(.48,.04,.04),(.15,.60,.72))
entity("Sun",(0,5,1),rotation=(-.7,-.5,0),LightComponent={"Type":0,"Color":[1,.94,.85],"Intensity":2.3,"CastShadows":True,"ShadowDistance":16,"ShadowBias":1,"ShadowPCFRadius":2})
entity("Cyan fill",(-2,1.7,1.6),LightComponent={"Type":1,"Color":[.25,.7,1],"Intensity":2,"Radius":8})
entity("Warm fill",(2,2,1),LightComponent={"Type":1,"Color":[1,.65,.28],"Intensity":2.5,"Radius":8})
entity("Camera",(0,2.5,6.8),rotation=(-.27,0,0),CameraComponent={"ProjectionType":0,"Primary":True,"FixedAspectRatio":False,"OrthographicNear":-1,"OrthographicFar":1,"AntiAliasing":True,"FrustumCulling":True,"PerspectiveFOV":48,"PerspectiveNear":.05,"PerspectiveFar":100})
entity("Showcase look",(0,0,0),ScriptComponent={"Name":"Showcase"})
scene={"Scene":"Materials","SkyColor":[.06,.09,.15],"AmbientColor":[.78,.85,1],"AmbientIntensity":.32,"Entities":entities}
(ROOT/"Assets/Scenes/Materials.scene").write_text(json.dumps(scene,indent=2)+"\n",encoding="utf-8")
project={"Project":{"Name":"Rendering Showcase","StartupScene":"Materials","GameModule":"Game.dll","Scenes":[{"Name":"Materials","Path":"Assets/Scenes/Materials.scene"}]}}
(ROOT/"Showcase.myproject").write_text(json.dumps(project,indent=2)+"\n",encoding="utf-8")
print("Generated standalone engine material gallery.")
