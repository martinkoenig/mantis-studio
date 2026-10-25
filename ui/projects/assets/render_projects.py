"""Original UI-M2a mechanical studies; offline Blender 4.3.2 CPU Cycles.
Reuse the accepted M1 primitive/material/lighting definitions without rendering
or modifying any Home file. No reference pixels or vendor CAD.
"""
from pathlib import Path
HOME = Path(__file__).resolve().parents[2] / 'home/assets/render_home.py'
exec(compile(HOME.read_text().split('# Foregrounds have no floor/world pixels.')[0], str(HOME), 'exec'))
OUT = str(Path(__file__).resolve().parent)
scene.render.film_transparent = True
scene.render.image_settings.color_mode = 'RGBA'
studio_floor.hide_render = True

def annulus(name, loc, radius, inner, depth, rotate=(0,0,0)):
    obj=cylinder(name,loc,radius,depth,edge,rotate)
    cut(obj,loc,inner,depth*3,rotate)
    return obj

def engine():
    b=box('Four-cylinder block',(0,0,.7),(2.7,1.7,1.35),bevel=.12)
    for x in [-.94,-.31,.31,.94]:
        cut(b,(x,0,.85),.26,2)
        for y in [-.65,.65]: cut(b,(x,y,1.3),.055,.4)
    for x in [-.94,-.31,.31,.94]:
        annulus('Side bearing',(x,-.91,.7),.29,.17,.17,(math.pi/2,0,0))
    for y in [-.62,-.31,0,.31,.62]: box('Casting ribs',(.0,y,.13),(2.7,.07,.12))

def pipe():
    # A bent hollow tube from an original toroidal section.
    bpy.ops.mesh.primitive_torus_add(major_radius=.8,minor_radius=.3,major_segments=72,minor_segments=24,location=(0,0,.8),rotation=(math.pi/2,0,0))
    o=finish(bpy.context.object,metal,.0)
    # Two original flange mouths read clearly as an intake elbow study.
    annulus('Intake mouth',(-.8,-.16,.8),.46,.24,.26,(math.pi/2,0,0))
    annulus('Intake flange',(.8,-.16,.8),.48,.24,.26,(math.pi/2,0,0))
    for x in [-.8,.8]:
        for i in range(4):
            a=math.tau*i/4
            cylinder('Mount fastener',(x+math.cos(a)*.38,-.32,.8+math.sin(a)*.38),.048,.06,edge,(math.pi/2,0,0))

def enclosure():
    b=box('Open electronics enclosure',(0,0,.35),(2.75,2,.64),bevel=.12)
    # Rectangular pocket boolean, original dimensions.
    bpy.ops.mesh.primitive_cube_add(size=1,location=(0,0,.57))
    tool=bpy.context.object;tool.dimensions=(2.37,1.6,.66);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    m=b.modifiers.new('Electronics pocket','BOOLEAN');m.object=tool;m.operation='DIFFERENCE'
    bpy.context.view_layer.objects.active=b;bpy.ops.object.modifier_apply(modifier=m.name);bpy.data.objects.remove(tool,do_unlink=True)
    pcb=material('Illustrative board',(0.025,.25,.11),.22,.4)
    box('Controller board',(0,0,.2),(2.1,1.4,.06),pcb,.025)
    for x in [-.66,0,.66]:
        box('Package',(x,.12,.29),(.4,.55,.14),black,.025)
        for y in [-.26,.5]:
            for j in range(5):box('Pin',(x-.15+j*.075,y,.28),(.034,.18,.035),edge,.004)
    for x in [-1.13,1.13]:
        for y in [-.73,.73]:annulus('Mount pillar',(x,y,.45),.12,.055,.5)

def gear():
    annulus('Gear housing',(0,0,.5),1,.53,.7)
    for i in range(28):
        a=math.tau*i/28
        o=box('Cast spline',(math.cos(a)*1.01,math.sin(a)*1.01,.5),(.18,.13,.59),bevel=.02);o.rotation_euler.z=a
    annulus('Bearing lip',(0,0,.9),.74,.51,.1)
    for i in range(6):
        a=math.tau*i/6;cut(objects[0],(math.cos(a)*.81,math.sin(a)*.81,.5),.065,1.2)

def manifold():
    b=box('Manifold block',(0,0,.55),(2.8,1.6,1),bevel=.25)
    for x in [-.88,0,.88]:
        annulus('Port boss',(x,-.88,.6),.34,.2,.38,(math.pi/2,0,0))
        cut(b,(x,0,.6),.2,2,(math.pi/2,0,0))
        annulus('Top connection',(x,0,1.1),.26,.15,.3)
        cut(b,(x,0,.55),.15,1.5)
    for x in [-1.18,1.18]:
        for y in [-.55,.55]: cut(b,(x,y,.55),.075,1.6)

def flange():
    b=cylinder('Mounting disc',(0,0,.2),1.25,.3)
    cut(b,(0,0,.2),.43,1)
    annulus('Long bearing sleeve',(0,0,.8),.58,.43,1.1)
    for i in range(8):
        a=math.tau*i/8;cut(b,(math.cos(a)*.98,math.sin(a)*.98,.2),.09,.9)

def heatmap(build):
    build()
    # Illustration only: a continuous position-derived palette, never measured QC.
    qc=material('Illustrative palette',(0,.3,1),.18,.48)
    nodes=qc.node_tree.nodes;links=qc.node_tree.links
    geo=nodes.new('ShaderNodeTexCoord');sep=nodes.new('ShaderNodeSeparateXYZ');ramp=nodes.new('ShaderNodeValToRGB')
    links.new(geo.outputs['Generated'],sep.inputs[0]);links.new(sep.outputs['X'],ramp.inputs[0])
    colors=[(.01,.03,.85,1),(.02,.75,1,1),(.04,.8,.18,1),(.95,.9,.01,1),(1,.16,.015,1)]
    cr=ramp.color_ramp
    cr.elements.remove(cr.elements[1]);cr.elements[0].color=colors[0]
    for i,c in enumerate(colors[1:],1):cr.elements.new(i/4).color=c
    links.new(ramp.outputs['Color'],nodes.get('Principled BSDF').inputs['Base Color'])
    for o in objects:o.data.materials.clear();o.data.materials.append(qc)

for name,build in [('engine',engine),('bracket-qc',lambda:heatmap(bracket)),('pipe',pipe),('enclosure',enclosure),('gear',gear),('manifold',manifold),('flange',flange),('cover-qc',lambda:heatmap(cover))]:
    build();render(name,(640,400),(4,-6,4.3),(0,0,.72),5.5);clear()
