"""MitiruEngine のレベルを Blender で作り、1 キーで glb に書き出すアドオン。

使い方は docs/LEVEL_FROM_BLENDER.md。Blender 4.2 以降 (5.x を含む) で動く。

- オブジェクトに `mitiru_type` (spawn / trigger / enemy / camera / collision / navmesh など) を付ける。
  屋外の印 (terrain / water / scatter) は world.json の "level" から読まれる (docs/TERRAIN_AND_WATER.md)。
  それ以外のカスタムプロパティはそのまま glTF の extras に入り、エンジンの level::LevelData から読める。
- Ctrl+Alt+E (3D ビュー) か File > Export > Mitiru Level で、シーンの書き出し先へ glb を書く。
  一時ファイルに書いてから置き換えるので、`mitiru_host --watch-assets` は書きかけを読まない。
- 書き出し先を `mitiru_host --watch-assets <フォルダ>` の下に置けば、実行中のゲームが読み直す。
- Export Camera Path は、カメラのアニメをカットシーンのカメラの鍵 (docs/CUTSCENES.md の `path`) として JSON に書く。
"""

import json
import math
import os
import struct

import bpy

bl_info = {
    "name": "Mitiru Level Export",
    "author": "MitiruEngine",
    "version": (1, 2, 0),
    "blender": (4, 2, 0),
    "location": "3D View > Sidebar > Mitiru / File > Export > Mitiru Level",
    "description": "Blender のシーンを MitiruEngine のレベル (glb + extras) として書き出す",
    "category": "Import-Export",
}

TYPE_KEY = "mitiru_type"
SIZE_KEY = "mitiru_size"
MARK_TYPES = ("spawn", "trigger", "enemy", "camera", "collision", "navmesh", "terrain", "water", "scatter")
VOLUME_TYPES = {"trigger", "camera", "water", "scatter"}
SURFACE_TYPES = {"collision", "navmesh"}
SETTING_KEYS = ("mitiru_level_path", "mitiru_level_export_on_save", "mitiru_camera_path", "mitiru_camera_step")


def export_path(scene):
    return bpy.path.abspath(scene.mitiru_level_path)


def sync_sizes(scene):
    """Empty の表示サイズを mitiru_size に写す。glTF には表示サイズの欄が無く、トリガーの箱の大きさに要る。"""
    for obj in scene.objects:
        if obj.type != "EMPTY" or TYPE_KEY not in obj:
            continue
        size = float(obj.empty_display_size)
        # 同じ値を書き直すとファイルが未保存の扱いになるので、違うときだけ書く
        if obj.get(SIZE_KEY) != size:
            obj[SIZE_KEY] = size


def problems(scene):
    """書き出しても意図どおりに読めないものを集める。"""
    found = []
    for obj in scene.objects:
        kind = obj.get(TYPE_KEY, "")
        if kind in SURFACE_TYPES and obj.type != "MESH":
            found.append(f"{obj.name}: mitiru_type={kind} はメッシュにしか付けられない")
        if kind and not isinstance(kind, str):
            found.append(f"{obj.name}: mitiru_type は文字列にする")
        if kind == "scatter" and not isinstance(obj.get("model"), str):
            found.append(f"{obj.name}: mitiru_type=scatter には撒く glb を model (文字列) で付ける")
    return found


def strip_settings(glb_path):
    """glTF の出力はシーンに登録したプロパティも extras に書く。書き出し先 (手元の絶対パスのこともある) を
    ゲームのデータに残さないよう、このアドオンの設定だけを取り除く。"""
    with open(glb_path, "rb") as f:
        data = f.read()
    json_len = struct.unpack_from("<I", data, 12)[0]
    doc = json.loads(data[20:20 + json_len])
    for scene in doc.get("scenes", []):
        extras = scene.get("extras", {})
        for key in SETTING_KEYS:
            extras.pop(key, None)
        if not extras:
            scene.pop("extras", None)
    text = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    rest = data[20 + json_len:]
    header = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + len(rest))
    with open(glb_path, "wb") as f:
        f.write(header + struct.pack("<II", len(text), 0x4E4F534A) + text + rest)


def write_level(scene, path):
    """一時ファイル (先頭が '.') に書いてから置き換える。"""
    folder, name = os.path.split(path)
    os.makedirs(folder or ".", exist_ok=True)
    tmp = os.path.join(folder, "." + os.path.splitext(name)[0] + ".export.glb")
    sync_sizes(scene)
    bpy.ops.export_scene.gltf(
        filepath=tmp,
        export_format="GLB",
        export_extras=True,
        export_yup=True,
        export_apply=True,
        export_cameras=False,
        export_lights=False,
        export_animations=False,
    )
    strip_settings(tmp)
    os.replace(tmp, path)


class MITIRU_OT_export_level(bpy.types.Operator):
    """シーンを MitiruEngine のレベル (glb) として書き出す"""

    bl_idname = "mitiru.export_level"
    bl_label = "Export Mitiru Level"

    def execute(self, context):
        scene = context.scene
        for message in problems(scene):
            self.report({"WARNING"}, message)
        path = export_path(scene)
        if not path.lower().endswith(".glb"):
            self.report({"ERROR"}, f"書き出し先は .glb にする: {path}")
            return {"CANCELLED"}
        write_level(scene, path)
        self.report({"INFO"}, f"Mitiru level -> {path}")
        return {"FINISHED"}


def to_engine(v):
    """Blender (Z が上) の座標を、glTF の書き出しと同じくエンジン (Y が上、右手系) の座標にする。"""
    return (v[0], v[2], -v[1])


def camera_key(obj, t):
    """カメラの今のフレームを、カットシーンの鍵 (docs/CUTSCENES.md) にする。roll はエンジンの camera3D と同じ向き。"""
    m = obj.matrix_world
    eye = to_engine(m.translation)
    forward = to_engine(-m.col[2].xyz.normalized())
    up = to_engine(m.col[1].xyz.normalized())
    focus = obj.data.dof.focus_distance if obj.data.dof.focus_distance > 0.0 else 10.0
    target = tuple(e + f * focus for e, f in zip(eye, forward))
    base = (0.0, 0.0, 1.0) if abs(forward[1]) > 0.999 else (0.0, 1.0, 0.0)
    right = cross(forward, base)
    up0 = cross(right, forward)
    roll = math.degrees(math.atan2(dot(up, right) / length(right), dot(up, up0) / length(up0)))
    return {"t": round(t, 4), "eye": [round(c, 4) for c in eye], "target": [round(c, 4) for c in target],
            "fov": round(math.degrees(obj.data.angle_y), 3), "roll": round(roll, 3)}


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def length(a):
    return max(math.sqrt(dot(a, a)), 1e-9)


def write_camera_path(scene, obj, path):
    """frame_start から frame_end まで camera_step フレームおきに鍵を取り、{"keys": [...]} を書く。"""
    fps = scene.render.fps / scene.render.fps_base
    step = max(1, scene.mitiru_camera_step)
    frames = list(range(scene.frame_start, scene.frame_end + 1, step))
    if frames[-1] != scene.frame_end:
        frames.append(scene.frame_end)
    current = scene.frame_current
    keys = []
    for frame in frames:
        scene.frame_set(frame)
        keys.append(camera_key(obj, (frame - scene.frame_start) / fps))
    scene.frame_set(current)
    folder, name = os.path.split(path)
    os.makedirs(folder or ".", exist_ok=True)
    tmp = os.path.join(folder, "." + name + ".tmp")
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump({"keys": keys}, f, indent=1)
    os.replace(tmp, path)
    return len(keys)


class MITIRU_OT_export_camera_path(bpy.types.Operator):
    """アクティブなカメラ (無ければシーンのカメラ) の動きを、カットシーンのカメラの鍵として書き出す"""

    bl_idname = "mitiru.export_camera_path"
    bl_label = "Export Camera Path"

    def execute(self, context):
        scene = context.scene
        obj = context.active_object if context.active_object and context.active_object.type == "CAMERA" else scene.camera
        if obj is None:
            self.report({"ERROR"}, "カメラを選ぶか、シーンのカメラを決める")
            return {"CANCELLED"}
        path = bpy.path.abspath(scene.mitiru_camera_path)
        count = write_camera_path(scene, obj, path)
        self.report({"INFO"}, f"Mitiru camera path ({count} keys) -> {path}")
        return {"FINISHED"}


class MITIRU_OT_mark(bpy.types.Operator):
    """選んだオブジェクトに mitiru_type を付ける (空なら外す)"""

    bl_idname = "mitiru.mark"
    bl_label = "Mark for Mitiru"
    bl_options = {"REGISTER", "UNDO"}

    kind: bpy.props.StringProperty(name="Type", default="")

    def execute(self, context):
        for obj in context.selected_objects:
            if not self.kind:
                obj.pop(TYPE_KEY, None)
                obj.pop(SIZE_KEY, None)
                continue
            obj[TYPE_KEY] = self.kind
            if obj.type == "EMPTY" and self.kind in VOLUME_TYPES:
                obj.empty_display_type = "CUBE"
        return {"FINISHED"}


class MITIRU_PT_level(bpy.types.Panel):
    bl_label = "Mitiru Level"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "Mitiru"

    def draw(self, context):
        layout = self.layout
        scene = context.scene
        layout.prop(scene, "mitiru_level_path", text="")
        layout.prop(scene, "mitiru_level_export_on_save")
        layout.operator(MITIRU_OT_export_level.bl_idname, icon="EXPORT")
        layout.prop(scene, "mitiru_camera_path", text="")
        layout.prop(scene, "mitiru_camera_step")
        layout.operator(MITIRU_OT_export_camera_path.bl_idname, icon="CAMERA_DATA")
        obj = context.active_object
        if obj is None:
            return
        layout.label(text=f"{obj.name}: {obj.get(TYPE_KEY, '-')}")
        grid = layout.grid_flow(columns=3, align=True)
        for kind in MARK_TYPES:
            grid.operator(MITIRU_OT_mark.bl_idname, text=kind).kind = kind
        layout.operator(MITIRU_OT_mark.bl_idname, text="clear").kind = ""


def menu_export(self, context):
    self.layout.operator(MITIRU_OT_export_level.bl_idname, text="Mitiru Level (.glb)")


@bpy.app.handlers.persistent
def export_after_save(*_args):
    scene = bpy.context.scene
    if scene is not None and scene.mitiru_level_export_on_save and not problems(scene):
        write_level(scene, export_path(scene))


CLASSES = (MITIRU_OT_export_level, MITIRU_OT_export_camera_path, MITIRU_OT_mark, MITIRU_PT_level)
_keymaps = []


def register():
    bpy.types.Scene.mitiru_level_path = bpy.props.StringProperty(
        name="Level file", subtype="FILE_PATH", default="//level.glb")
    bpy.types.Scene.mitiru_level_export_on_save = bpy.props.BoolProperty(
        name="Export on save", default=False)
    bpy.types.Scene.mitiru_camera_path = bpy.props.StringProperty(
        name="Camera path file", subtype="FILE_PATH", default="//camera.camera.json")
    bpy.types.Scene.mitiru_camera_step = bpy.props.IntProperty(
        name="Key every N frames", default=5, min=1)
    for cls in CLASSES:
        bpy.utils.register_class(cls)
    bpy.types.TOPBAR_MT_file_export.append(menu_export)
    bpy.app.handlers.save_post.append(export_after_save)
    keyconfig = bpy.context.window_manager.keyconfigs.addon
    if keyconfig is not None:
        keymap = keyconfig.keymaps.new(name="3D View", space_type="VIEW_3D")
        item = keymap.keymap_items.new(MITIRU_OT_export_level.bl_idname, "E", "PRESS", ctrl=True, alt=True)
        _keymaps.append((keymap, item))


def unregister():
    for keymap, item in _keymaps:
        keymap.keymap_items.remove(item)
    _keymaps.clear()
    if export_after_save in bpy.app.handlers.save_post:
        bpy.app.handlers.save_post.remove(export_after_save)
    bpy.types.TOPBAR_MT_file_export.remove(menu_export)
    for cls in reversed(CLASSES):
        bpy.utils.unregister_class(cls)
    del bpy.types.Scene.mitiru_camera_step
    del bpy.types.Scene.mitiru_camera_path
    del bpy.types.Scene.mitiru_level_export_on_save
    del bpy.types.Scene.mitiru_level_path
