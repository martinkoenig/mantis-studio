"""Original UI-M1 illustrative assets. Blender 4+, offline CPU Cycles.
Run: blender --background --python ui/home/assets/render_home.py
No scanner model/vendor geometry or reference pixels are used.
"""
import bpy
import math
import os
import sys
from mathutils import Vector

OUT = os.path.dirname(os.path.abspath(__file__))
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 256
scene.cycles.seed = 0
scene.cycles.use_denoising = False
scene.render.image_settings.file_format = 'PNG'
scene.render.resolution_percentage = 100
scene.world.color = (0.09, 0.09, 0.09)
scene.view_settings.view_transform = 'AgX'


def material(name, color, metallic=0, roughness=0.4):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Metallic'].default_value = metallic
    p.inputs['Roughness'].default_value = roughness
    return m

metal = material('Bead blasted aluminium', (0.42, 0.49, 0.50), 0.82, 0.3)
edge = material('Machined rim', (0.68, 0.73, 0.74), 0.9, 0.23)
black = material('Graphite scanner shell', (0.024, 0.035, 0.042), 0.55, 0.28)
rubber = material('Inset matte elastomer', (0.009, 0.017, 0.020), 0.1, 0.55)
glass = material('Optical glass illustration', (0.012, 0.035, 0.038), 0.75, 0.13)
mint = material('Mint accent', (0.025, 0.8, 0.5), 0.4, 0.25)
blue = material('Blue inset', (0.01, 0.32, 0.55), 0.5, 0.2)
floor = material('Dark teal studio', (0.013, 0.029, 0.031), 0.25, 0.48)
objects = []


def finish(obj, mat, bevel=0.035):
    obj.data.materials.append(mat)
    if bevel:
        mod = obj.modifiers.new('Machined edge bevel', 'BEVEL')
        mod.width = bevel
        mod.segments = 3
    if obj.type == 'MESH':
        for p in obj.data.polygons:
            p.use_smooth = True
        mod = obj.modifiers.new('Weighted normals', 'WEIGHTED_NORMAL')
    objects.append(obj)
    return obj


def box(name, loc, scale, mat=metal, bevel=0.035):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.object
    o.name = name
    o.dimensions = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return finish(o, mat, bevel)


def cylinder(name, loc, radius, depth, mat=metal, rotate=(0, 0, 0), vertices=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=loc, rotation=rotate)
    o = bpy.context.object
    o.name = name
    return finish(o, mat, 0.018)


def cut(target, loc, radius, depth, rotate=(0, 0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=radius, depth=depth, location=loc, rotation=rotate)
    tool = bpy.context.object
    mod = target.modifiers.new('Bore', 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.object = tool
    bpy.context.view_layer.objects.active = target
    while target.modifiers[0] != mod:
        bpy.ops.object.modifier_move_up(modifier=mod.name)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(tool, do_unlink=True)


def housing():
    flange = cylinder('Eight mounting lug casting', (0, 0, 0.18), 1.4, 0.24, vertices=8)
    # Upright annular casing, not a gear: deep circular inspection opening.
    body = cylinder('Deep cast housing', (0, 0, 0.66), 1.10, 1.06)
    cut(body, (0, 0, 0.92), 0.80, 1.1)
    cut(flange, (0, 0, 0.4), 0.65, 1.4)
    rim = cylinder('Polished inspection face', (0, 0, 1.21), 1.12, 0.10, edge)
    cut(rim, (0, 0, 1.22), 0.81, 0.5)
    for i in range(8):
        a = i * math.tau / 8
        x, y = math.cos(a), math.sin(a)
        lug = cylinder('Mounting boss', (x * 1.19, y * 1.19, 0.33), 0.20, 0.30)
        cut(lug, (x * 1.19, y * 1.19, 0.33), 0.088, 0.8)
        cut(flange, (x * 1.19, y * 1.19, 0.18), 0.088, 0.8)
        rib = box('Casting web', (x * 1.05, y * 1.05, 0.63), (0.46, 0.13, 0.68))
        rib.rotation_euler.z = a
        cut(rim, (x * 0.98, y * 0.98, 1.2), 0.052, 0.5)
    cylinder('Inner bearing seat', (0, 0, 0.25), 0.40, 0.14, edge)
    for i in range(4):
        a = i * math.tau / 4
        cylinder('Recess fastener', (math.cos(a)*0.67, math.sin(a)*0.67, 0.25), 0.08, 0.12, edge)


def rotor():
    cylinder('Impeller base', (0, 0, 0.16), 1.24, 0.16)
    bpy.ops.mesh.primitive_cone_add(vertices=64, radius1=0.42, radius2=0.28, depth=1.35, location=(0, 0, 0.83))
    hub = finish(bpy.context.object, edge)
    cut(hub, (0, 0, 1.46), 0.17, 0.38)
    for i in range(12):
        a = i * math.tau / 12
        verts, faces = [], []
        for j in range(15):
            t = j / 14
            r = 0.35 + t * 0.88
            angle = a + 0.66 * t
            z = 1.40 * (1-t)**0.8 + 0.22
            for offset in [-0.027, 0.027]:
                verts.extend([(r*math.cos(angle+offset), r*math.sin(angle+offset), 0.23),
                              (r*math.cos(angle+offset), r*math.sin(angle+offset), z)])
        for j in range(14):
            k = j*4
            faces.extend([(k,k+4,k+5,k+1), (k+2,k+3,k+7,k+6),
                          (k+1,k+5,k+7,k+3), (k,k+2,k+6,k+4)])
        faces.extend([(0,1,3,2), (56,58,59,57)])
        mesh = bpy.data.meshes.new('Swept blade mesh')
        mesh.from_pydata(verts, [], faces)
        o = bpy.data.objects.new('Swept impeller blade', mesh)
        bpy.context.collection.objects.link(o)
        finish(o, metal, 0.012)


def bracket():
    base = box('Mounting bracket foot', (0, 0, 0.22), (2.6, 1.55, 0.26), bevel=0.1)
    upright = box('Upright bracket flange', (-1.0, 0, 0.95), (0.25, 1.55, 1.5), bevel=0.075)
    for y in [-0.48, 0.48]:
        cut(base, (0.86, y, 0.22), 0.16, 1)
        cut(upright, (-1, y, 1.30), 0.17, 1, (0, math.pi/2, 0))
        verts = [(-0.84,y-0.055,0.34), (0.48,y-0.055,0.34), (-0.84,y-0.055,1.46),
                 (-0.84,y+0.055,0.34), (0.48,y+0.055,0.34), (-0.84,y+0.055,1.46)]
        mesh = bpy.data.meshes.new('Gusset mesh')
        mesh.from_pydata(verts, [], [(0,2,1),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)])
        o = bpy.data.objects.new('Triangular stiffening gusset', mesh)
        bpy.context.collection.objects.link(o)
        finish(o, metal)
    cut(base, (0.10, 0, 0.22), 0.24, 1)


def cover():
    plate = box('Closed gearbox inspection cover', (0, 0, 0.23), (2.75, 2.0, 0.3), bevel=0.16)
    top = box('Raised cover casting', (0, 0, 0.45), (2.27, 1.52, 0.35), bevel=0.14)
    for y in [-0.5, -0.25, 0, 0.25, 0.5]:
        box('Parallel cooling rib', (0, y, 0.69), (2.1, 0.095, 0.16), bevel=0.045)
    for x in [-1.18, 1.18]:
        for y in [-0.80, 0.80]:
            cut(plate, (x,y,0.23), 0.095, 1)
    cylinder('Inspection plug', (0,0,0.78), 0.22, 0.14, edge, vertices=6)


def scanner():
    box('Scanner pedestal', (0,0,0.15), (1.35,1.1,0.28), black, 0.09)
    box('Scanner stand', (0,0.18,0.81), (0.42,0.55,1.3), black, 0.09)
    box('Unbranded scanner shell', (0,0,1.95), (0.92,0.64,2.05), black, 0.16)
    box('Machined face bezel', (0,-0.339,1.95), (0.76,0.10,1.86), edge, 0.13)
    box('Optical inset', (0,-0.404,1.99), (0.53,0.06,1.56), rubber, 0.12)
    for z in [1.45, 2.55]:
        cylinder('Lens barrel', (0,-0.46,z), 0.175, 0.12, black, (math.pi/2,0,0))
        cylinder('Optical lens', (0,-0.53,z), 0.123, 0.02, glass, (math.pi/2,0,0))
        cylinder('Lens ring', (0,-0.55,z), 0.035, 0.009, blue, (math.pi/2,0,0))
    box('Illustrative blue inset', (0,-0.45,1.99), (0.12,0.018,0.28), blue, 0.04)
    box('Mint design accent', (0,-0.41,1.18), (0.19,0.018,0.045), mint, 0.013)
    for i in range(6):
        box('Vent slit', (0.464,0.05,1.75+i*0.11), (0.016,0.33,0.03), rubber, 0.006)


def light(name, loc, energy, color, size):
    bpy.ops.object.light_add(type='AREA', location=loc)
    o = bpy.context.object
    o.name = name
    o.data.energy = energy
    o.data.color = color
    o.data.shape = 'DISK'
    o.data.size = size
    o.rotation_euler = (Vector((0,0,0.5)) - o.location).to_track_quat('-Z','Y').to_euler()

studio_floor = box('Studio floor', (0,0,-0.10), (200,200,0.1), floor, 0)
objects.clear()
light('Soft overhead', (-3,-4,7), 1100, (0.82,0.91,1), 5)
light('Mint rim', (2,4,4), 1250, (0.26,0.75,0.61), 4)
light('Warm edge', (4,-1,3), 700, (0.91,0.96,1), 3)
bpy.ops.object.camera_add()
cam = bpy.context.object
scene.camera = cam
cam.data.type = 'ORTHO'


def render(name, size, loc, target, scale):
    cam.location = loc
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.data.ortho_scale = scale
    scene.render.resolution_x, scene.render.resolution_y = size
    scene.render.filepath = os.path.join(OUT,name+'.png')
    bpy.ops.render.render(write_still=True)


def clear():
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)
    objects.clear()

# Foregrounds have no floor/world pixels. Lighting remains identical to the
# scenic pass; the card, safe area and contact shadow belong to QML.
scene.render.film_transparent = True
scene.render.image_settings.color_mode = 'RGBA'
studio_floor.hide_render = True
for name, build in [('housing',housing),('rotor',rotor),('bracket',bracket),('cover',cover)]:
    build()
    render(name, (640,380), (4,-6,4.3), (0,0,0.85 if name == 'bracket' else 0.65), 4.9 if name == 'bracket' else 4.3)
    clear()
scanner()
render('scanner', (480,400), (4,-6,3.2), (0,0,1.48), 4.5)
clear()
# Allow foreground-only reproduction without rewriting the accepted hero.
if '--subjects-only' in sys.argv:
    sys.exit(0)
scene.render.film_transparent = False
scene.render.image_settings.color_mode = 'RGB'
studio_floor.hide_render = False
# Hero subjects occupy the right half; dark negative space supports real QML type.
housing()
for o in objects:
    o.location += Vector((1.4,0.3,0))
objects.clear()
scanner()
for o in objects:
    o.location += Vector((4.0,0.5,0))
render('hero', (1600,480), (7,-12,5.0), (0.7,0,1.35), 13.0)
