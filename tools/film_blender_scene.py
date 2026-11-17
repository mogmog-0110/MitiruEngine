"""mitiru_film export-blender が scene.json の隣へ書く、Blender (Cycles) で 1 コマ撮るスクリプト。docs/FILM_PT.md、ADR 0154。

    blender -b -P render_blender.py -- --out frame.png [--samples 128] [--gpu] [--denoise] [--watt-scale 40]

世界の軸は film (y が上) のまま scene.json に入っていて、ここで Blender の z 上へ入れ替える。
素材は glTF の PBR をそのまま使う。点光は強さ (raster の単位) に --watt-scale を掛けた W にする。
"""
import json
import math
import sys
import time
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

HERE = Path(__file__).resolve().parent
# film の (x, y, z) -> Blender の (x, -z, y)。glTF のインポートが物に掛ける変換と同じ
TO_BLENDER = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
SENSOR_HEIGHT_MM = 20.25


def parse_args():
	argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
	opt = {"out": None, "samples": 128, "gpu": False, "denoise": False, "watt_scale": 40.0, "seed": 0}
	i = 0
	while i < len(argv):
		a = argv[i]
		if a == "--out":
			opt[a[2:]] = argv[i + 1]
			i += 2
		elif a in ("--samples", "--seed"):
			opt[a[2:]] = int(argv[i + 1])
			i += 2
		elif a == "--watt-scale":
			opt["watt_scale"] = float(argv[i + 1])
			i += 2
		elif a in ("--gpu", "--denoise"):
			opt[a[2:]] = True
			i += 1
		else:
			raise SystemExit(f"知らない引数: {a}")
	if not opt["out"]:
		raise SystemExit("--out が要ります")
	return opt


def film_matrix(m):
	"""scene.json の 3x4 (行優先) を Blender の世界の 4x4 にする"""
	film = Matrix(((m[0], m[1], m[2], m[3]), (m[4], m[5], m[6], m[7]), (m[8], m[9], m[10], m[11]), (0, 0, 0, 1)))
	return TO_BLENDER @ film @ TO_BLENDER.inverted()


def add_prop(prop, root):
	if not prop["model"]:
		print(f"警告: {prop['name']} は組み込みの形 ({prop['shape']}) で、このスクリプトは描きません")
		return
	path = (root / prop["model"]).resolve()
	if not path.is_file():
		print(f"警告: {prop['name']} のモデルが見つかりません: {path}")
		return
	before = set(bpy.data.objects)
	bpy.ops.import_scene.gltf(filepath=str(path))
	fresh = [o for o in bpy.data.objects if o not in before]
	world = film_matrix(prop["matrix"])
	for o in fresh:
		if o.parent is None or o.parent not in fresh:
			o.matrix_world = world @ o.matrix_world
	for o in fresh:
		o.name = f"{prop['name']}.{o.name}"


def add_lights(lights, watt_scale):
	for l in lights:
		kind = {"sun": "SUN", "point": "POINT", "spot": "SPOT"}[l["kind"]]
		data = bpy.data.lights.new(l["name"], kind)
		data.color = l["color"]
		if kind == "SUN":
			data.energy = l["intensity"]
		else:
			data.energy = l["intensity"] * watt_scale
			data.shadow_soft_size = 0.05
		obj = bpy.data.objects.new(l["name"], data)
		bpy.context.scene.collection.objects.link(obj)
		obj.location = TO_BLENDER @ Vector(l["pos"])
		if kind != "POINT":
			d = TO_BLENDER.to_3x3() @ Vector(l["dir"])
			obj.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
			if kind == "SPOT":
				data.spot_size = math.radians(l["outerDeg"]) * 2.0


def add_camera(cam, size):
	data = bpy.data.cameras.new("film")
	data.sensor_fit = "VERTICAL"
	data.sensor_height = SENSOR_HEIGHT_MM
	data.lens = SENSOR_HEIGHT_MM / 2.0 / math.tan(math.radians(cam["fovDeg"]) / 2.0)
	data.clip_start, data.clip_end = cam["near"], cam["far"]
	if cam["focusDist"] > 0 and cam["apertureRadius"] > 0:
		data.dof.use_dof = True
		data.dof.focus_distance = cam["focusDist"]
		data.dof.aperture_fstop = data.lens / (2000.0 * cam["apertureRadius"])
	obj = bpy.data.objects.new("film", data)
	bpy.context.scene.collection.objects.link(obj)
	eye = TO_BLENDER @ Vector(cam["eye"])
	look = (TO_BLENDER @ Vector(cam["target"])) - eye
	up = TO_BLENDER.to_3x3() @ Vector(cam["up"])
	fwd = look.normalized()
	right = fwd.cross(up).normalized()
	true_up = right.cross(fwd).normalized()
	obj.matrix_world = Matrix.Translation(eye) @ Matrix((right, true_up, -fwd)).transposed().to_4x4()
	bpy.context.scene.camera = obj
	bpy.context.scene.render.resolution_x, bpy.context.scene.render.resolution_y = size
	bpy.context.scene.render.resolution_percentage = 100


def set_world(world):
	sc = bpy.context.scene
	w = bpy.data.worlds.new("film")
	w.use_nodes = True
	bg = next(n for n in w.node_tree.nodes if n.type == "BACKGROUND")
	amb = world["ambient"]
	if any(world["ambientSky"]) or any(world["ambientGround"]):
		amb = [(a + b) * 0.5 for a, b in zip(world["ambientSky"], world["ambientGround"])]
	bg.inputs["Color"].default_value = (amb[0], amb[1], amb[2], 1.0)
	bg.inputs["Strength"].default_value = 1.0
	sc.world = w
	sc.view_settings.view_transform = "Standard"
	sc.view_settings.look = "None"
	sc.view_settings.exposure = math.log2(max(world["exposure"], 1e-4))


def set_cycles(opt):
	sc = bpy.context.scene
	sc.render.engine = "CYCLES"
	cy = sc.cycles
	cy.samples = opt["samples"]
	cy.seed = opt["seed"]
	cy.use_animated_seed = False
	cy.use_denoising = opt["denoise"]
	if opt["denoise"]:
		cy.denoiser = "OPENIMAGEDENOISE"
		cy.denoising_input_passes = "RGB_ALBEDO_NORMAL"   # 補助パスは Cycles が自分で取り、同じ OIDN に渡す
	if opt["gpu"]:
		prefs = bpy.context.preferences.addons["cycles"].preferences
		prefs.compute_device_type = "OPTIX"
		prefs.get_devices()
		for d in prefs.devices:
			d.use = d.type == "OPTIX"
		cy.device = "GPU"
	sc.render.image_settings.file_format = "PNG"
	sc.render.image_settings.color_depth = "8"
	sc.render.film_transparent = False


def main():
	opt = parse_args()
	scene = json.loads((HERE / "scene.json").read_text(encoding="utf-8"))
	bpy.ops.wm.read_factory_settings(use_empty=True)
	for k, n in scene.get("unsupported", {}).items():
		if n:
			print(f"警告: {k} が {n} 件あり、このスクリプトは描きません")
	for p in scene["props"]:
		add_prop(p, HERE / "models")
	add_lights(scene["lights"], opt["watt_scale"])
	add_camera(scene["camera"], scene["size"])
	set_world(scene["world"])
	set_cycles(opt)
	bpy.context.scene.render.filepath = opt["out"]
	started = time.perf_counter()
	bpy.ops.render.render(write_still=True)
	print(f"書きました: {opt['out']} (render {time.perf_counter() - started:.2f} 秒、{opt['samples']} spp)")


main()
