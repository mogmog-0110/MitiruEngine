"""口と目と眉のシェイプキーを持つ簡単な頭 (球) を glb に書き出す。映像の props[].morphs の実物確認用 (docs/FILM_MORPH.md)。

    blender -b -P tools/blender/film_talk_head.py -- examples/film_player/assets/models/talk_head.glb

顔は glTF の +Z を向く (Blender では -Y)。口 (Mouth) は頭の前に置いた平らな円盤で、シェイプキー aa ih ou ee oh smile が頂点を動かす。
目 (Eye_L, Eye_R) は blink、眉 (Brow_L, Brow_R) は browUp と browDown。名前は mesh.extras.targetNames に出る。
"""
import math
import sys

import bmesh
import bpy

R = 0.5                     # 頭の半径 (m)
MOUTH_Y = -(R - 0.0275)     # 口の円盤の面。頭の表面の少し前
MOUTH_Z = -0.17
CLOSED = (0.075, 0.009)     # 閉じた口の半幅と半高 (m)
VISEMES = {                 # 半幅、半高
	"aa": (0.115, 0.105),
	"ih": (0.150, 0.026),
	"ou": (0.045, 0.060),
	"ee": (0.165, 0.045),
	"oh": (0.075, 0.095),
}
SMILE = (0.130, 0.040, 0.075)   # 半幅、半高、両端の持ち上がり


def material(name, rgb):
	m = bpy.data.materials.new(name)
	m.use_nodes = True
	bsdf = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
	bsdf.inputs["Base Color"].default_value = (*rgb, 1.0)
	bsdf.inputs["Roughness"].default_value = 0.8
	return m


def link(obj, mat):
	bpy.context.collection.objects.link(obj)
	obj.data.materials.append(mat)
	return obj


def sphere(name, radius, loc, mat, scale=(1, 1, 1), segs=32, rings=16):
	bm = bmesh.new()
	bmesh.ops.create_uvsphere(bm, u_segments=segs, v_segments=rings, radius=radius)
	mesh = bpy.data.meshes.new(name)
	bm.to_mesh(mesh)
	bm.free()
	obj = bpy.data.objects.new(name, mesh)
	obj.location = loc
	obj.scale = scale
	return link(obj, mat)


def disc(name, loc, mat, segs=28):
	"""x と z に広がる円盤 (法線は -Y 側、つまり glTF の +Z 側)。中心の頂点 1 つ + 外周の扇形"""
	mesh = bpy.data.meshes.new(name)
	verts = [(0.0, 0.0, 0.0)] + [(math.cos(a), 0.0, math.sin(a)) for a in (2 * math.pi * i / segs for i in range(segs))]
	faces = [(0, 1 + (i + 1) % segs, 1 + i) for i in range(segs)]
	mesh.from_pydata(verts, [], faces)
	mesh.update()
	obj = bpy.data.objects.new(name, mesh)
	obj.location = loc
	return link(obj, mat)


def shape(obj, name, move):
	"""basis の頂点を move(i, (x, y, z)) で動かしたシェイプキーを足す"""
	if obj.data.shape_keys is None:
		obj.shape_key_add(name="Basis", from_mix=False)
	key = obj.shape_key_add(name=name, from_mix=False)
	key.value = 0.0   # 新しいキーの値は 1 で入り、glb の mesh.weights の既定になって全部の形が重なる
	for i, d in enumerate(obj.data.vertices):
		key.data[i].co = move(i, tuple(d.co))


def on_surface(x, z):
	"""口の円盤の頂点 (ローカルの x, z) を頭の球に沿わせる y。平らな円盤は、口が大きく開くと上の端が球の中へ潜って切れて見える"""
	wz = MOUTH_Z + z
	return -math.sqrt(max(R * R - x * x - wz * wz, 0.0)) - 0.01 - MOUTH_Y


def build():
	for o in list(bpy.data.objects):
		bpy.data.objects.remove(o)
	skin = material("skin", (0.86, 0.66, 0.55))
	dark = material("mouth", (0.22, 0.03, 0.04))
	black = material("eye", (0.02, 0.02, 0.03))
	brow_m = material("brow", (0.18, 0.1, 0.06))

	sphere("Head", R, (0, 0, 0), skin, segs=48, rings=24)
	sphere("Nose", 0.06, (0, -(R + 0.005), -0.05), skin, scale=(0.8, 1.2, 0.9), segs=16, rings=8)

	mouth = disc("Mouth", (0, MOUTH_Y, MOUTH_Z), dark)
	cw, ch = CLOSED
	for d in mouth.data.vertices:
		x, z = d.co[0] * cw, d.co[2] * ch
		d.co = (x, on_surface(x, z), z)
	for name, (hw, hh) in VISEMES.items():
		shape(mouth, name, lambda i, co, hw=hw, hh=hh: (co[0] / cw * hw, on_surface(co[0] / cw * hw, co[2] / ch * hh), co[2] / ch * hh))
	sw, sh, lift = SMILE
	shape(mouth, "smile", lambda i, co: (co[0] / cw * sw, on_surface(co[0] / cw * sw, co[2] / ch * sh + lift * (co[0] / cw) ** 2),
	                                    co[2] / ch * sh + lift * (co[0] / cw) ** 2))

	for side, x in (("L", 0.17), ("R", -0.17)):
		eye = sphere(f"Eye_{side}", 0.052, (x, -(R - 0.065), 0.09), black, segs=16, rings=8)
		shape(eye, "blink", lambda i, co: (co[0], co[1], co[2] * 0.08))
		brow = bpy.data.objects.new(f"Brow_{side}", bpy.data.meshes.new(f"Brow_{side}"))
		bm = bmesh.new()
		bmesh.ops.create_cube(bm, size=1.0)
		bmesh.ops.scale(bm, vec=(0.15, 0.025, 0.026), verts=bm.verts)
		bm.to_mesh(brow.data)
		bm.free()
		brow.location = (x, -(R - 0.05), 0.2)
		link(brow, brow_m)
		tilt = 1.0 if side == "L" else -1.0
		shape(brow, "browUp", lambda i, co: (co[0], co[1], co[2] + 0.06))
		shape(brow, "browDown", lambda i, co, tilt=tilt: (co[0], co[1], co[2] - 0.035 - tilt * co[0] * 0.5))


def export(path):
	bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_morph=True, export_morph_normal=True, export_apply=False,
	                          export_yup=True, export_cameras=False, export_lights=False)


if __name__ == "__main__":
	args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
	if len(args) != 1:
		sys.exit("使い方: blender -b -P film_talk_head.py -- <出力.glb>")
	build()
	export(args[0])
	print("wrote", args[0])
