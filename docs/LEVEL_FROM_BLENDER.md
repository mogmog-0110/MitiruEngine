# Blender でレベルを作る

MitiruEngine はレベルエディタを持たない。Blender をレベルエディタとして使い、glb に書き出したものをエンジンが読む。
同じ glb を、見た目は `drawModel` が描き、配置と当たり判定とナビメッシュの元は `level::LevelData` が読む。
キャラクターと動作は FBX のままでも渡せる (最後の節)。

## アドオンを入れる

`tools/blender/mitiru_level_export.py` を、Blender (4.2 以降) の Edit > Preferences > Add-ons > Install from Disk で入れて有効にする。
3D ビューのサイドバーに Mitiru のタブが出る。

## 印を付ける

オブジェクトのカスタムプロパティ `mitiru_type` で役割を決める。タブのボタンで選んだオブジェクトに付けられる。

| `mitiru_type` | 付けるもの | エンジンでの扱い |
|---|---|---|
| `spawn` `enemy` `camera` など (名前はゲームが決める) | Empty | `Entity` になる。名前、型、世界座標の位置・回転・拡大、ほかのカスタムプロパティを持つ |
| `trigger` | 立方体表示の Empty | `Entity`。`halfExtents` が箱の半分の大きさ (拡大 × 表示サイズ) |
| `collision` | メッシュ | 当たり判定だけの面。`collision()` に入り、描かない |
| `navmesh` | メッシュ | ナビメッシュの元だけの面。`navSource()` に入り、描かない |

- `mitiru_type` を付けたオブジェクトは描かない。
- 見た目のメッシュに `mitiru_collide` (真) を付けると、その面も `collision()` に入る。`mitiru_nav` (真) なら `navSource()` に入る。
- ほかのカスタムプロパティは、数・真偽・文字列 (63 バイトまで)・2〜4 個の数の配列を読む。名前は 31 バイトまで。
  入れ子のプロパティなど読めないものは飛ばし、`LevelLoadResult::warnings` に理由を残す。

## 書き出す

タブで書き出し先の .glb を決め、Ctrl+Alt+E (3D ビュー) か File > Export > Mitiru Level で書き出す。
Export on save を入れると、.blend を保存するたびに書き出す。

座標は glTF と同じで、Y が上、1 単位が 1 m。Blender の (x, y, z) は (x, z, -y) になる。

## ゲームから読む

```cpp
#include <mitiru/level/LevelLoader.hpp>

// LevelData は GameMemory の外に置く。ファイルの中身だけで決まるので、読み直せば同じものができる
static mitiru::level::LevelFile g_level("arena/assets/levels/arena.glb");

void update(Input in, Hud hud, float dt)
{
	g_level.reloadIf(in.actionPayload("asset.reloaded"));   // 書き出し直したら読み直す
	const auto& level = g_level.get();
	if (const auto* spawn = level.find("PlayerSpawn")) { /* spawn->position */ }
	level.forEachOfType("enemy", [&](const mitiru::level::Entity& e, std::size_t index) {
		const float hp = level.number(e, "hp", 10.0f);
	});
}

void draw(Screen& s) const
{
	s.drawModel("arena/assets/levels/arena.glb", {0.0f, 0.0f, 0.0f});
}
```

GameMemory には Entity の添字か `nameHash` を入れ、ポインタは入れない。巻き戻しと録画にレベルのファイルの中身は入らない
([DETERMINISM.md](DETERMINISM.md))。

## 実行中に読み直す

`mitiru_host <game.dll> --watch-assets <フォルダ>` で起動する。フォルダの中の .glb / .gltf / .obj / .fbx が書き換わると、
描画は次の `drawModel` で読み直し、ゲームには `asset.reloaded` (`{"path": フォルダからの相対パス}`) が届く。
`LevelFile::reloadIf` はその path がレベルのファイルを指すときだけ読み直す。書き出し途中のファイルで読めなかったときは、前の中身を残す。

## 当たり判定とナビメッシュ

`collision()` と `navSource()` は世界座標の三角形の列 (`positions` は xyz、`indices` は 3 つで 1 枚)。
`mitiru_navbake arena.glb -o arena.navmesh` は、ナビメッシュの元の面があればその面だけで焼く。

## キャラクターと動作 (FBX)

`drawModel` / `drawModelBlend` には .fbx もそのまま渡せる。初回に隣へ `<名前>.fbx.glb` を作り (ufbx で変換)、以後はそれを読む。
FBX が新しくなれば作り直す。pack で配るときは、変換済みの `.fbx.glb` を入れる。

- 単位と軸は glTF にそろえる。Mixamo の cm・Y-up も、Z-up の FBX も、1 単位 1 m・Y-up になる。
- AnimStack が動作になる。Blender の `Armature|Run` は glTF の書き出しと同じく `Run` という名前になる。動作は線形のキーに焼く。
- 頂点が使わない骨は joint にしない。メッシュ 1 つにつきスキンは 1 つ、頂点あたり骨 4 本まで。モーフ (シェイプキー) は読まない。
- 動作だけの FBX (メッシュが無いもの) は読めない。動作はモデルと同じファイルに入れる (Mixamo なら With Skin で落とすか、Blender で 1 つにまとめる)。
- テクスチャは FBX に埋め込んだものか、FBX の隣や相対パスにある画像を使う。
- Mixamo の FBX はゲームに入れて配れるが、FBX のファイルそのものを再配布してはいけない。
