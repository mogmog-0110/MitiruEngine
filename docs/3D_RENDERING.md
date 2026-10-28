# 3D Rendering — 機能対応状況とロードマップ

> このドキュメントは MitiruEngine の 3D 描画機能の **「何が動いて、何が未完成で、何が実験段階か」** を一元化した参照表。
> 新規 consumer が 3D を使い始める前に必ず確認すること。
> 関連: `docs/SCOPE.md` / `docs/ARCHITECTURE.md`

---

## TL;DR

- **動く**: DX11 Phong / Toon / NPR メッシュ描画 + Skybox + 単一光源 / アンビエント / 8 ライト対応 multi-light Phong + outline (DX12) + glTF/OBJ ロード + CPU deferred pipeline + DX12 の 4x MSAA (MSAA color → HDR intermediate resolve → tonemap) + DX12 の Weighted-Blended OIT（`render/dx12/WeightedBlendedOIT.hpp`、無条件配線）+ **DX12 の PBR (Cook-Torrance) + IBL**（`ShaderMode3D::PBR`、`ISceneFx::setEnvironment`、golden 1 枚。下記の簡略化あり）+ **DX12 の 2〜3 カスケード指向性シャドウ**（`ISceneFx::setCascadedShadowEnabled` / `setShadowCascadeCount`、golden 3 枚。下記の簡略化あり）
- **未完成だが API はある**: 共通 `Skybox` クラスの DX12 path（`Renderer3D_DX12` 自体は内蔵の別経路で skybox 対応済み）
- **間接光 (DX12、既定は無効、ADR 0070、[LIGHTING_GI.md](LIGHTING_GI.md))**: `mitiru_lightbake` で焼く SH L2 の放射照度のプローブ (DDGI の距離の地図で壁の向こうを落とす。Toon は段に刻める) と箱で映す反射のプローブ、前のフレームの HZB を前方の PS で辿る画面の反射 (SSR、PBR と水面)
- **experimental（使用注意）**: SSS（`render/SubsurfaceScatter.hpp`、DX11 のみで未配線）。Tessellation は現状コードベースに存在しない
- **未実装**: PBR/IBL の DX11 GPU 出力・WebGPU 3D・4 カスケード以上（3 カスケードまでと自動 split・視錐台フィットは実装済み）

---

## 座標の向き (ADR 0029)

右手系で Y が上、1 単位が 1 m。カメラが -Z を向くと +X が画面の右、+Z を向くと +X が画面の左に出る。
`camera3D` を呼ばないときは目 (6, 5, 8) から (0, 0.5, 0) を見る。既知の座標に箱を置いて撮った画面は
[ACTION_LIBRARY.md の「画面の左右と座標の向き」](ACTION_LIBRARY.md#画面の左右と座標の向き)。

## 色の約束 (ADR 0061)

- 3D の描画に渡す色 (`Material::diffuse` / `specular`、光、環境光、フォグ、頂点色、インスタンスの色、デカール・粒・剣筋・
  地形の色、clear の色) は、16 進や CSS と同じ sRGB で書いた値。GPU へ上げる所で線形にし、光を線形で足す
  (`render/ColorSpace.hpp`)。1 を超える色は「1 に収めた色 × 倍率」として扱う
- トーンマップは中立の肩 (線形 0.9 まで恒等、その先は最大成分で色全体を縮める) と sRGB。白い光 (強さ 1) が正面から当たった面と
  照明なしの面は、書いた色のまま出る (成分 243 以下は ±3)。`gamma` は sRGB に対する表示の補正で、2.2 が sRGB そのもの
- PBR の直接光は「光の色 × π」を放射照度とみなす (Toon / Phong と同じ明るさの約束)
- glTF の係数と `COLOR_0` は読み込みで sRGB へ寄せる。PBR の `MaterialMaps::baseColor` だけは線形のまま

## 副ビューとモデルの解放 (ADR 0061、ゲーム DLL からは ABI v49 / ADR 0062)

- `Renderer3D_DX12::createView` / `beginView` / `endView` で、主ビューと同じフレームの中に別のカメラの絵を描く。
  間に積んだメッシュ・局所光・デカール・半透明・屋外・粒・剣筋・Effekseer はそのビューにだけ出る (ビューごとに世界を描く)。
  bloom・FXAA・TAA・SSAO は主ビューの設定に合わせて掛かる。TAA の履歴・ずらし・動きベクトルと SSAO の遮蔽はビューごとに持つ
  (主ビューが GTAO でも副ビューは SSAO)。影はビューごとに自分のカメラへ合わせた影マップ (主ビューと同じ段数のカスケード、
  一辺は主ビューと同じか `View3DDesc::shadowMapSize`) で受けるので、分割画面の 2 人が遠く離れてもどちらにも影が落ちる。
  空を張るビュー (`View3DDesc::skybox`) は空気遠近と体積フォグの froxel もビューごとに持つ (空の LUT は主ビューのもの)。
  輪郭線・動きのぼけ・FSR と clod の世界は主ビューだけ。主ビューに何も描かず、貼り付けが出力の全画素を覆うフレームは、
  主ビューの後処理を飛ばす (絵は変わらない。`mainPostSkipped()` で分かる)。
  出力は `compositeView` で出力画面の矩形へ貼る (分割画面・小窓) か、`drawMeshWithView` で材質の色に使う
  (モニターは前のフレームの絵)。分割画面の見本は `examples/coop3d` (2 人が 44〜116 m 離れる参道)
- ゲーム DLL は 0〜7 の slot で使う: `s.view3D(slot, View3DPod)` (同じ作りなら作り直さない) → `s.beginView3D(slot, eye, target, up,
  Deg{fov}, near, far)` → 描く → `s.endView3D()`。貼るのは `s.compositeView3D(slot, x, y, w, h)` (論理座標)、板に映すのは
  `s.drawMeshWithView3D(shape, pos, scale, rot, color, slot)`、UI に出すのは RML の `<img src="view3d:N"/>`。見本は `examples/views3d`。
  View3DPod の flags の bit2..5 (`kView3DNoTemporal` / `NoAmbientOcclusion` / `NoBloom` / `NoAntiAlias`) でビューごとに後処理を止め、
  `shadowMapSize` で影マップの一辺を決める (ABI v50、ADR 0067)。NoTemporal は主ビューが TAA でも FXAA にする
- `Renderer3D_DX12::releaseModel(path)` (ゲーム DLL は `s.releaseModel(path)`) は glTF / FBX のモデルを手放す。GPU が読み終えてから消える
- 1 フレームに描けるスキン描画は 2048 prim で、超えた分は 1 度だけ警告して描かない (ADR 0065)

## HDR の環境 (ABI v62 の入口、ADR 0099 の I5、ADR 0110)

`s.environmentHdr3D("assets/sky/day_field_2k.hdr", 1.0f, Deg{0.0f})` で、正距円筒の HDR 画像 (Radiance の .hdr) を空と PBR の環境光 (IBL) にします。
明るさは 1 を超えたまま RGBA16F の cubemap に持つので、太陽は金属や水に明るいまま映ります。空の name で、入れる前の `environment3D` / `skybox3D` の勾配に戻ります。
画像の約束、費用、録画での扱い、Dawn は [HDR_ENVIRONMENT.md](HDR_ENVIRONMENT.md) に書きました。

## 毎フレーム張り直す面 (ABI v54、ADR 0076)

粒から Surface Nets や marching cubes で張り直すゼリーや液体の面は、頂点の数も並びもフレームごとに変わる。
`s.updateMesh3D(name, vertices, n, indices, m, prevPositions)` で送ると、`prevPositions[i]` (頂点 `i` の点が前のフレームに
あった位置、`vertices` と同じ座標系) から頂点ごとの動きベクトルを作り、TAA・FSR・動きのぼけが残像を引かない。

```cpp
// 1 回だけ: 最大の数を容量として登録する (中身は 0 でよい。以後この中では GPU のバッファを作らない)
static std::vector<render::Vertex3D> zeros(kMaxVertices);
static std::vector<std::uint32_t> zeroIdx(kMaxIndices, 0);
if (!s.hasMesh3D("jelly")) { s.registerMesh3D("jelly", zeros.data(), kMaxVertices, zeroIdx.data(), kMaxIndices); }

// 毎フレーム: 張り直した面と、各頂点が前のフレームにあった位置 (粒の速度から p - v·dt など)
s.updateMesh3D("jelly", surf.vertices.data(), n, surf.indices.data(), m, surf.prevPositions.data());
s.drawMesh("jelly", {0, 0, 0}, {1, 1, 1}, {0, 0, 0}, hex(0x7FD8C8));
```

- 登録した数が容量になり、host は先頭の `n` 頂点・`m` 添字だけを書く。減った分の添字は潰れた三角形 (0, 0, 0) で埋める。
  VB・IB・前の位置の VB は同じ大きさのまま回すので、GPU が読んでいるバッファへ CPU が書くことはない
- 容量を超えたら 2 の冪へ広げ、1 度だけ警告する。登録せずに呼んでも、初回に作る
- 法線は渡したまま使う。面の法線へ置き換えたり、カメラの側へ向け直したりしない (滑らかな陰影は頂点の法線で作る)。
  表は反時計回りの側で、裏は他のメッシュと同じく描かない
- `prevPositions` が `nullptr` の頂点は動かなかった扱い。メッシュを `drawMesh` の行列で動かした分は、行列の差からも足される
- 前の位置が効くのは送ったフレームだけ。送らなかったフレームは止まった面として描く
- 添字が `n` 以上か、`m` が 3 の倍数でなければそのフレームは何もしない (1 度だけ警告)
- 30k 頂点・約 18 万添字で、送り込みは開発機の Debug ビルドで 1 フレーム数 ms (`TestTopologyMeshUploadDx12` が測る)。
  頂点の数が変わらない布は今までどおり `updateMesh3D(name, vertices, n)` で送る ([PHYSICS_FX.md](PHYSICS_FX.md))

## カメラと的の間の面を透かす (ABI v54、ADR 0077)

三人称のカメラを壁から逃がさず、カメラとプレイヤーの間でプレイヤーを隠す面の方を網点で抜く。毎フレーム 1 回呼ぶ。

```cpp
render::OccluderFade fade = render::OccluderFade::around(playerCenter, 0.8f);   // 体を包む球
fade.silhouette[0] = fade.silhouette[1] = fade.silhouette[2] = 1.0f;          // 隠れた所の影絵 (書いた sRGB)
fade.silhouette[3] = 0.45f;                                                     // 濃さ。0 なら描かない
s.fadeOccluders3D(fade);
s.fadeTarget3D(true);    // プレイヤー自身は抜かず、影絵の形にする
drawPlayer(s);
s.fadeTarget3D(false);
```

- 抜くのは、目から的の球を見込む円錐の中で、的の中心より手前にあり、的がその面の裏にある画素だけ。的の足元の床や、
  的が貼り付いて走る壁は抜けない。円が小さすぎる時は `minScreenRadius` (画面の高さに対する半径、既定 0.1) まで広げる
- 縁は `edge` の幅でぼかし、`strength` で中心の抜け方を決める (1 で全部、0.8 で 2 割を網点として残す)
- 網点は 4x4 の Bayer。TAA か TAAU の間はフレームごとにずらして履歴で混ぜ、止めた絵では毎フレーム同じ網点になる
- 前方の描画 (instancing、スキン、updateMesh3D の面を含む) と clod の両方で抜く。clod は抜いた画素に奥の clod の面が残る
- 抜いた面も影を落とし、DDGI の光も遮る。主ビューだけに効く
- 的を `fadeTarget3D` で挟まないと、的の中心より手前にある的自身の面も抜けて半透明になる
- 1080p の街 (1600 個の箱、影と TAA) で GPU の増えは約 0.02 ms (`mitiru_tests_render.exe "[occluder_fade_perf]"`)

## 描画ごとのトゥーン (ABI v54、ADR 0079)

`s.toonStyle3D(&style)` を呼ぶと、以後の描画が `style` の段・縁の帯・つや・輪郭線で描かれる。`style.metal` で金属の見た目にもできる (ABI v57、[TOON_METAL.md](TOON_METAL.md))。`nullptr` で場面の値
(`sceneLook3D` の `toonBands` と `toon3D` の影の色、`outline3D`) へ戻り、フレーム頭でも戻る。街と主人公で別の見た目にできる。

```cpp
render::ToonStyle city;                       // 街: 2 段、境 0.05、影は塗った色 × (0.56, 0.58, 0.72)
city.bands = 2;
city.thresholds[0] = 0.05f;                   // 太陽が高くても日の当たる壁は日なたの段
float shade[3] = {0.56f, 0.58f, 0.72f};
for (int i = 0; i < 3; ++i) { city.bandTint[0][i] = shade[i]; city.bandTint[1][i] = 1.0f; }
city.outlineColor[0] = 0x26 / 255.0f; city.outlineColor[1] = 0x20 / 255.0f; city.outlineColor[2] = 0x40 / 255.0f;
city.outlineMix = 0.7f;                       // 下の色 30% + 線の色 70%
city.outlineWidthPx = 2.0f;                   // 1080p で 2 px
city.outlineFarWidthPx = 1.0f; city.outlineFarDistance = 25.0f;   // 25 m より先は 1 px
city.outlineFadeStart = 70.0f; city.outlineFadeEnd = 140.0f;      // 70〜140 m で消える

render::ToonStyle jelly = city;               // 主人公: 境 0.15、影は 0.68 倍、つや 1 つ、濃い緑の 3 px は消さない
jelly.thresholds[0] = 0.15f;
for (int i = 0; i < 3; ++i) { jelly.bandTint[0][i] = 0.68f; }
jelly.glossMinNdotH = 0.985f;
jelly.outlineColor[0] = 0x1F / 255.0f; jelly.outlineColor[1] = 0x3A / 255.0f; jelly.outlineColor[2] = 0x12 / 255.0f;
jelly.outlineMix = 1.0f; jelly.outlineWidthPx = 3.0f;
jelly.outlineFarWidthPx = 0.0f; jelly.outlineFadeEnd = 0.0f;
jelly.flags = render::kToonKeepColorInImpact;  // 止め絵の間も色のまま

s.toonStyle3D(&city);
drawCity(s);
s.toonStyle3D(&jelly);
s.drawMesh("jelly", ...);                     // updateMesh3D で張り直す面でも同じ
s.toonStyle3D(nullptr);
```

- 段: `bands` (1〜4) と、暗い側から昇順の境 `thresholds`、段ごとに塗った色へ掛ける `bandTint` (書いた sRGB)。`bands` が 0 なら
  塗りは場面の値のままで、線と flags だけを使う。境は画素 1 個ぶんの幅で切るので、`softness` が 0 でもぎざぎざにならない。
  落ちた影の中では段を `shadowSteps` だけ下げる (0 は一番暗い段)。段は陰影がトゥーン (`toon3D` か `shadingModel` 2) の時だけ効く
- 縁の帯: N·V が `rimMaxNdotV` 未満で、影も含めた N·L が `rimMaxNdotL` 未満の所を `rimColor` で塗る。つや: N·H が `glossMinNdotH`
  を超え、落ちた影の外の所を `glossColor` で塗る。どちらも光の色が掛かる。描画ごとの段では場面の段付きハイライトと縁光は使わない。
  つやの強さは `glossStrength` (0〜1 は塗った色との混ぜ具合、1 を超えた分は `glossColor` を足して光らせる)。帯の縁の幅は `glossSoftness` (N·H) と
  `rimSoftness` (N·V) で、`softness` と同じく全幅で渡す。既定 (1、0、0) の絵は今までと同じ (ABI v60、ADR 0086)。
  環境光と焼いた光は場面の値のまま足すので、段の色をそのまま出したい時は環境光を 0 にする
- 線: 太さは縦 1080 画素の画面での画素数で、画面の高さに比例する (720p なら 2/3)。1 画素より細くなっても途切れず、濃さが太さの分になる。
  線は物の外形 (深さが 4% 以上奥へ跳ぶ所)、違う指定の物との境、面の向きが約 37° 以上折れる所に引く。テクスチャの模様には引かない。
  線は外形の手前側の画素を中心に引くので、太さの半分ほどが物の上に乗る
- 同じフレームに違う線の指定は 7 個まで使える (同じ指定は使い回す)。超えた描画は場面の線になり、1 度だけ警告する。
  指定を 1 つでも使ったフレームは場面の線も同じ方式で引く (太さは今までの線と同じく `outlineWidthPx` の整数部の 2 倍の画素)。
  使わないフレームは今までの線のまま
- clod の物は、フレームで最初に積んだ clod の物の時の段と線で塗る。clod の直接光も前方の描画と同じ段になる。clod の面は深さと
  指定の境で線に入り、面の向きの折れ目では入らない
- 張り直す面 (ADR 0076) の線は深さと指定の境から取るので、頂点の並びが毎フレーム変わっても揺れない

### 止め絵

```cpp
render::ImpactFrame f;
f.mode = render::ImpactFrameMode::TwoTone;   // 輝度が threshold 以上を lightColor (#F6EEDC)、未満を darkColor (#262040)
s.impactFrame3D(f);                          // そのフレームだけ効く。3 フレーム出すなら 3 フレーム渡す
```

`Invert` は色を反転する。`amount` で元の色から寄せる割合を決める。TAA・アップスケールの後、当たりの演出 (HitFeel) の前に掛けるので、
TAA の履歴に残らない。`kToonKeepColorInImpact` を立てた `ToonStyle` で描いた物は色のまま残る。

### 平らな 2 色の粒

`ParticleShape::HardDisc` は縁の硬い円、`ParticleBlend::Opaque` は不透明で深度を書く混ぜ方。2 色目は `particles3D` の 3 引数の版で、
エミッターと同じ並びの `ParticleFlatShade` で渡す。粒の円を影の色で塗り、`litOffset` (半径の何倍か、画面の右と上が正) だけずらした
明るい円を粒の色で重ねるので、反対側に影の三日月が残る。色の a は使わないので、消すときは `size` の曲線で 0 へ縮める。
不透明の粒は深度で隠れ合い、半透明の粒より先に描く。輪郭線は引かない。

### 費用

1920x1080 の Release、RTX 5070 Ti。1600 個の箱の街 (街の指定)、主人公 (主人公の指定)、不透明の土ぼこり 64 粒、止め絵を毎フレーム
(`mitiru_tests_render.exe "[toon_perf]"`)。

| パス | GPU 時間 |
|---|---|
| 線 (種と線の 2 パス) | 0.09 ms |
| 止め絵 | 0.02 ms |
| 粒 (刻みと描画) | 0.02 ms |

合わせて約 0.13 ms。段は描画の PS の中の分岐で、別のパスは無い。

---

## 機能 × backend の表 (DX12 と Dawn、ADR 0090)

次の表は `include/mitiru/render/RenderFeatures.hpp` から `python tools/gen_render_features_doc.py` が作る。手で書き換えず、行や対応を変えるときは `RenderFeatures.hpp` を直す。
`python tools/check_render_features.py` が、表と `IRenderer3D` / `ISceneFx` / `Screen` の入口の名前を突き合わせる。`mitiru_host --render-features` で同じ表を端末に出せる。

- 対応は同じ手法で描くこと。別手法は手法が違うが絵は許容内、弱いは描くが見た目が落ちる、なしは描かない
- 区分の基本は常に作る機能で、宣言は重い機能。`mitiru.toml` の `[render] needs` (DLL の export `mitiru_module_render_needs`) に書いた行だけを、renderer の初期化で作る。宣言しない行は代わりの描き方で続け、使われたら 1 回だけ知らせる
  - 宣言が作る時期を決めているのは、DX12 の `oit` `clod` `taa_motion_vectors` `motion_blur` `ambient_occlusion` `effekseer` `render_scale` と、Dawn 通常の `oit`。ほかの宣言の行は、宣言しても今までどおり使ったときに作る (`mitiru_host --render-features` の「宣言で作る」の欄)
  - `motion_blur` と `render_scale` は TAA の動きベクトルと履歴を使うので、宣言するとそれも作る
  - export の無い DLL は今までどおり全部を作る。環境変数 `MITIRU_RENDER_NEEDS` は `legacy` (宣言を使わない)、`none` (何も宣言しない)、名前の列 (その宣言) に切り替える
- Dawn が描けない依頼は、入口で数えて 1 回だけ知らせる。開発の実行では端末に 1 行、遊ぶ人の実行では詳細と `gpu_report.txt`

<!-- render-features:begin -->
| キー | 機能 | 区分 | DX12 | Dawn 通常 | Dawn 互換 | 備考 |
|---|---|---|---|---|---|---|
| `mesh_draw` | メッシュ | 基本 | 対応 | 対応 | 対応 | - |
| `model_gltf` | モデル | 基本 | 対応 | 対応 | 対応 | 金属・粗さの画像は読まない。スキンは CPU (絵は許容内) |
| `model_release` | モデルの解放 | 基本 | 対応 | なし | なし | 黙って false を返し、手放さない (絵は変わらず、メモリが増える) |
| `toon_style` | トゥーン | 基本 | 対応 | 対応 | 対応 | - |
| `shading_models` | 陰影の種類 | 基本 | 対応 | なし | なし | 黙ってトゥーンで描く (陰影の指定と環境 cubemap を無視) |
| `sun_shadow` | 太陽の影 | 基本 | 対応 | 対応 | 対応 | - |
| `outline` | 輪郭線 | 基本 | 対応 | 対応 | 別手法 | 互換は線形の深度を色に書いて線を作る。方式ごとの一致は未確認 |
| `bloom` | bloom | 基本 | 対応 | 対応 | 弱い | 互換は HDR が 1 で頭打ちなので、しきい値 1 では何も拾わない |
| `anti_alias` | アンチエイリアス | 基本 | 対応 | 対応 | 対応 | SMAA (aaMode 4、--aa smaa) は主ビューだけで、副ビューは FXAA。TAAU の間は掛けない。SMAA の表を入れていないビルドは FXAA |
| `tonemap_grade` | トーンマップと色調 | 基本 | 対応 | 対応 | 対応 | - |
| `color_lut` | 色の仕上げ | 基本 | 対応 | 対応 | 対応 | - |
| `wet_surface` | 濡れた面 | 基本 | 対応 | 弱い | 弱い | Dawn は粗さを持たないので、暗くすることと鏡面のつやを強めることだけ (PBR の粗さは下げない)。トゥーンのつやは場面の toonSpecular (既定 0) を上げたときだけ出て、既定では暗くなるだけ。DDGI が使う面の色は変わらない。地形 (OutdoorWorld) の地面は濡れない |
| `cloud_layer` | 雲の層 | 基本 | 対応 | 別手法 | 別手法 | 物理ベースの空 (sky_atmosphere を宣言して SkyLook::enabled) の上にだけ描く。cubemap の空と、単色・グラデーションの空には描かない。Dawn の空は手法が違う (sky_atmosphere と同じ) |
| `hdr_output` | HDR 出力 | 基本 | 対応 | なし | なし | 固定した Dawn は色管理 (SurfaceColorManagement) を受け付けず、SDR のまま出す。頼まれたときだけ数える |
| `sky_atmosphere` | 空と空気遠近 | 宣言 | 対応 | 別手法 | 別手法 | 空気遠近は画素ごとの 10 歩の積分 (DX12 は 32^3 の froxel)。setSkyboxEnabled(false) は効かない |
| `volumetric_fog` | 体積フォグ | 宣言 | 対応 | 別手法 | 別手法 | 画素ごとの 24 歩の積分。時間方向の混ぜと局所光の散乱は無い |
| `baked_gi` | 焼いた間接光 | 基本 | 対応 | 対応 | 対応 | - |
| `reflection_probes` | 反射のプローブ | 基本 | 対応 | なし | なし | 黙って無視 (IndirectLightLook の flag) |
| `ssr` | 画面の反射 | 宣言 | 対応 | なし | なし | 黙って無視 (IndirectLightLook の flag) |
| `ddgi` | 動く間接光 | 宣言 | 対応 | なし | なし | 黙って焼いた光のまま (IndirectLightLook の flag) |
| `local_lights` | 局所光 | 基本 | 対応 | 弱い | 弱い | 全灯を画素ごとに回す (256 灯まで)。スポットの影は無い |
| `multi_light` | 複数の光 | 基本 | 対応 | 弱い | 弱い | 先頭の 1 灯だけを使う (インターフェースの注記どおり。setUseMultiLight は何もしない) |
| `point_shadow` | 点光源の影 | 宣言 | 対応 | なし | なし | 黙って影なし (castShadow を付けた点光源の影を描かない。Dawn は局所光が先頭 1 灯だけ) |
| `ambient_occlusion` | AO | 宣言 | 対応 | なし | なし | 黙って無視 |
| `depth_of_field` | 被写界深度 | 宣言 | 対応 | なし | なし | 黙って無視 |
| `auto_exposure` | 自動露出 | 宣言 | 対応 | なし | なし | 黙って固定の露出 (SceneLook::autoExposureSpeed を無視) |
| `contact_shadow` | 接触影 | 宣言 | 対応 | なし | なし | 黙って影なし (SceneLook::contactShadow を無視) |
| `taa_motion_vectors` | TAA と動きベクトル | 宣言 | 対応 | なし | なし | 黙って MSAA だけで描く (aaMode に TAA を頼んでも) |
| `motion_blur` | 動きのぼけ | 宣言 | 対応 | なし | なし | 黙って無視 |
| `render_scale` | 内部解像度 | 宣言 | 対応 | 弱い | 弱い | 倍率は双線形 + FXAA で引き伸ばす。影の大きさの依頼は黙って無視 |
| `fsr3` | FSR 3.1 | 宣言 | 対応 | なし | なし | 黙って双線形 + FXAA |
| `oit` | 半透明の WBOIT | 宣言 | 対応 | 対応 | 弱い | 互換は遠い順の混ぜ |
| `decals` | デカール | 基本 | 対応 | 対応 | 対応 | froxel の絞りが無く、全部を画素ごとに回す |
| `gpu_particles` | 粒 | 基本 | 対応 | 弱い | 弱い | CPU で進める板ポリ。深度との衝突は無い |
| `vfx_textures` | VFX テクスチャ | 基本 | 対応 | 対応 | 対応 | - |
| `hit_feel` | ヒットの演出 | 基本 | 対応 | なし | なし | 黙って何もしない |
| `impact_frame` | 止め絵 | 基本 | 対応 | 対応 | 対応 | FXAA の後に掛ける (Dawn に TAAU が無い。絵は許容内、ADR 0087 §20) |
| `occluder_fade` | 透かし | 基本 | 対応 | 対応 | 別手法 | 互換は標本ごとの比べができず、4 標本の距離の割合で濃さを決める (影絵の縁が少し滑らか、ADR 0087 §20) |
| `trails` | 剣筋 | 基本 | 対応 | なし | なし | 黙って何も描かない |
| `screen_projection` | 3D の点の画面投影 | 基本 | 対応 | なし | なし | 黙って false を返す (投影が出ない。3D の debug draw が出ない) |
| `outdoor_world` | 屋外の地形・草・水 | 宣言 | 対応 | なし | なし | 黙って何も描かない (地形・草・水が出ない) |
| `clod` | 仮想ジオメトリ | 宣言 | 対応 | なし | なし | .clod / .obj のモデルを描かない (drawModel も、drawSkinnedModel に渡した .clod も) |
| `view_slots` | 副ビュー | 基本 | 対応 | 弱い | 弱い | 粒、デカール、局所光、フォグ、物理ベースの空、bloom、輪郭線が副ビューに出ない |
| `effekseer` | Effekseer | 宣言 | 対応 | なし | なし | .efkefc のエフェクトを描かない |
| `neural_fx` | ニューラルの後処理 | 宣言 | 対応 | なし | なし | 黙って何もしない (experimental() が空) |
| `neural_style_depth` | ONNX の後処理 | 宣言 | 対応 | なし | なし | 黙って何もしない (experimental() が空) |
| `splat_live2d` | スプラットと Live2D | 宣言 | 対応 | なし | なし | 黙って何もしない (experimental() が空) |
| `user_material` | 自作の材質 | 宣言 | 対応 | 対応 | 弱い | Dawn はトゥーンの陰影だけ (場面が Phong・PBR でも)で、tools/bake_materials.py で焼いた .wgsl を読む。半透明の描画には効かない。alpha を色に混ぜるのは DX12 と Dawn 通常で、互換は不透明のまま描く |
| `material_layers` | PBR の層 | 基本 | 対応 | なし | なし | 黙って層なし (glTF のクリアコート・異方性・シーンを読んでも描かない。Dawn に PBR の陰影が無い) |
| `light_2d` | 2D ライト | 宣言 | 対応 | 対応 | 対応 | 光は描く順に効き (呼ぶ前に描いた物には当たらない)、法線マップは sprite の id に _n を付けた同じ大きさの画像。クリアの色、SDF の角丸・円、3D と同じフレームの 2D、CPU 描画には当たらない。DX11 は対象外。Dawn は光の間だけ画像を GPU で描く (光なしは今までどおり) |
| `morph_targets` | モーフターゲット | 基本 | 対応 | 対応 | 対応 | - |
| `hdr_environment` | HDR の環境 | 基本 | 対応 | なし | なし | 今の環境光と空のまま描き、1 度だけ知らせる (Dawn に PBR の陰影と環境の cubemap が無い)。DX12 は RGBA16F の cubemap に写し、1 を超える明るさを空と環境光に保つ |
| `no_3d` | 3D を描かない | 宣言 | 対応 | 対応 | 対応 | - |
<!-- render-features:end -->

---

## 旧 backend の対応マトリクス (DX11 / Vulkan / OpenGL / WebGL / Null)

3D の機能を新しく足さない旧 backend の履歴で、更新しない。DX12 と Dawn は上の表を見る。

| 機能 | DX11 | DX12 | Vulkan | OpenGL | WebGL | Null | テスト |
|---|:-:|:-:|:-:|:-:|:-:|:-:|---|
| Camera3D / Mesh / Material（CPU） | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | `TestCamera3D`, `TestRender3D`, `TestMeshCache` |
| Phong（単一光源） | ✓ | ✓ | — | — | partial（実装あり、自動テスト無し） | no-op | `TestRenderer3DConfig` |
| **Phong（multi-light, 最大 8 灯）** | **✓** | **✓** | — | — | partial（`uLightCount` で最大 8 灯。自動テスト無し） | no-op | `TestLightArrayCB`, `TestRenderer3DConfig[multilight]`, `TestRenderer3D_DX12_Parity.cpp[multilight]` |
| Toon / NPR (10 種) | ✓ | partial | — | — | partial（Toon は出荷。対応は Phong / Toon / Unlit の 3 種） | no-op | `TestShaders3D` |
| Outline (depth Sobel/Laplacian/Color/Fresnel × 6 種) | no-op | ✓ | — | — | 出荷（色 + 法線/NdotV の MRT と深度から検出。DX12 と同じ式） | no-op | (GPU 動作のみ) |
| **Skybox / Cubemap** | **✓** | **✓**(Renderer3D_DX12 内蔵、`Skybox` クラス経由ではない) | — | — | partial（cubemap の +Y/-Y 面の中心色から上下 2 色のグラデーションを作るだけ） | no-op | `TestCubemap`, `TestSkybox[dx11]`, `TestRenderer3D_DX12_Parity.cpp[skybox]`, `TestDx12GoldenVrt.cpp` skybox gradient |
| 視錐台カリング (`Frustum`) | ✓ (`drawMesh`/`drawMeshInstanced` 配線、既定 ON) | ✓ (同左) | — | — | — | — | `TestFrustumCulling`, `TestCullingEdgeCases` |
| オクルージョンカリング (`OcclusionCuller`、CPU Hi-Z) | ✓ (`drawMesh`/`drawMeshInstanced` 配線、既定 OFF。4x MSAA 深度は min-depth resolve 経由で読み戻し、下記注記) | ✓ (同上、DX11 と同じ意味論。resolve→readback は非同期、下記注記) | — | — | — | — | `TestOcclusionCulling`, `TestDx12Occlusion` |
| GPU instancing (`drawMeshInstanced`) | ✓ (per-instance VB) | ✓ (per-instance VB、Toon/MultiLight のみ) | — | — | — | ループ呼び出しへフォールバック | `TestFrustumCulling.cpp[dx12]`, `TestRenderer3D_DX12_Parity.cpp` |
| PBR (Cook-Torrance, 単一光源) | partial（DX11 用シェーダー文字列はあるがコンパイル・配線されておらず未使用） | **✓**（`ShaderMode3D::PBR`、専用 root sig/PSO） | — | — | — | — | `TestDx12GoldenVrt.cpp[dx12_golden]` PBR spheres |
| **IBL / 環境ライティング** | 未実装 | **✓**（`ISceneFx::setEnvironment`。diffuse irradiance 1 枚 + specular prefilter の roughness 別 mip 連鎖 5 段 + split-sum の環境 BRDF 表 (`render/IblBrdfLut.hpp`、64²、R32G32)。シャドウ非対応） | 未実装 | 未実装 | 未実装 | — | 同上、`TestIblBrdfLut` |
| 影 (CPU shadow map, deferred pipeline 内) | ✓ | — | — | — | — | — | `TestShadowMap`, `TestShadowPass3D` |
| **影 (GPU cascaded shadow map)** | 未実装 | **✓**（2 カスケード。`ISceneFx::setCascadedShadowEnabled`、`shadowConfig().cascadeSplitDistance`/`cascadeNearHalfExtent` で手動調整、または `autoFitCascades` でカメラ視錐台から毎フレーム自動決定。カスケードを頼まない既定は、画質の上限までの段数を視錐台に合わせる。下記の簡略化あり） | — | — | —（単一の影マップは有り。カスケードは無い） | — | `TestDirectionalShadow`, `TestDx12GoldenVrt.cpp[dx12_golden]` cascaded shadow |
| 影の深度を描画と同じフレームの形で焼く | — | ✓（endFrame に別のリストへ積み、メインより先に流す。動く物や updateMesh3D で形を変える物が自分の影を受けない） | — | — | — | — | `TestShadowSameFrame` |
| 照明の後に足す光 (`drawMeshTinted` / `drawMeshInstancedTinted`、ADR 0075) | add は捨てる | ✓（影の中でも同じだけ明るい） | — | — | — | — | `TestMeshTintDx12` |
| Deferred pipeline (CPU rasterizer) | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | `TestDeferredPipeline` |
| glTF / OBJ ローダ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | `TestGltfLoader`, `TestObjLoader`, `TestMeshCache` |
| FBX ローダ | partial | partial | partial | partial | partial | — | — |
| Particle 3D (CPU) | ✓ | — | — | — | — | — | `TestParticleRenderer`, `TestParticleCollision` |
| Particle 3D (GPU compute) | **未完成** | — | — | — | — | — | `TestParticleCollision` |
| SSS | experimental（DX11 のみ、未配線） | 見送り（B15。材質フラグ設計が別スコープ） | — | — | — | — | `TestSubsurfaceScatter` |
| **Weighted-Blended OIT** | — | **✓**（無条件配線） | — | — | — | — | `TestWeightedBlendedOIT` |
| **焼いた GI (SH L2 プローブ)** | — | **✓**（`loadLightingBake` / `setGlobalIllumination`、Toon / Phong / PBR / 地形 / 草 / スキン。既定は無し） | — | — | — | — | `TestLightingBake`、`TestIndirectLightingDx12` |
| **反射のプローブ (box projection)** | — | **✓**（同じ焼いた光の中、PBR と水面） | — | — | — | — | 同上 |
| **SSR (HZB、前のフレーム)** | — | **✓**（`setScreenSpaceReflections`、PBR と水面、既定 OFF、副ビューでは無効） | — | — | — | — | `TestIndirectLightingDx12[ssr]`、`[bench_lighting]` |
| MSAA | 未実装 | **✓**（4x、MSAA color → HDR intermediate resolve → tonemap） | — | — | — | — | `TestDx12Msaa2D`（2D 側）、`TestDx12GoldenVrt` |
| TAA / FSR / FXAA / DOF | partial | partial | — | — | — | — | — |

Vulkan / OpenGL / Null の `createRenderer3DFor()`（`BackendInit.hpp`）挙動: Null は
`NullRenderer3D`（描画は行わず `drawCallCount` だけ数える）を返し、呼び出し側が
backend 分岐なしで動作を継続できる。Vulkan / OpenGL は 3D renderer が無いため
`warnOnce`（key: `render3d.vulkan.unsupported` / `render3d.opengl.unsupported`）で
初回のみ警告し `nullptr` を返す（2D パイプラインのみで動作継続）。

WebGL は `Renderer3D_WebGL`（`include/mitiru/render/Renderer3D_WebGL.hpp`、2026-08-22）を返す。それ以前は `nullptr` で、web ビルドでは 3D の地形や小物がまるごと消えていた。トゥーンの帯・影の色味・アウトラインは DX12 版の式をそのまま移植してある。

Metal は `feat/macos-support` ブランチにある（ADR 0031、main へは未マージ）。2D、深度バッファ、平行光 1 灯の Lambert、glTF（テクスチャ付き）、CPU スキニングまで。compute・ポストプロセス・影・MSAA・WBOIT・HDR は無い。main へ入るまで上の表には列を立てない。

凡例:
- ✓ = production 使用可能（テストあり）
- partial = ビルドは通る / 一部経路は動作 / 検証不足
- 未完成 = `SCOPE.md` で incomplete 宣言（v1 ロードマップ）
- experimental = `include/mitiru/render/experimental/` に隔離、`MITIRU_ENABLE_EXPERIMENTAL_RENDER` 要
- 未実装 = ヘッダ含めて存在しない
- 出荷 = 実作品 (ブラウザ版「オスカーのガーデニング」、COMITIA157 頒布) で使っている。自動テストは無い (WebGL は Emscripten 専用で、ネイティブのテストから呼べない)
- no-op = ヘッダ存在 + コンパイル通るが呼び出し時に何もしない（仕様）

---

## API エントリポイント

### `IRenderer3D`（`include/mitiru/render/IRenderer3D.hpp`）

DX11 / DX12 統一抽象。3D 描画はすべてこれ経由。

```cpp
class IRenderer3D {
public:
    virtual void beginFrame(const sgc::Colorf& clearColor) = 0;
    virtual void endFrame() = 0;
    virtual void setCamera(const Camera3D&) = 0;
    virtual void setLight(const Light&) = 0;
    virtual void setLights(std::span<const Light>);  // ←追加 (v2026.05.12)
    virtual void drawMesh(const Mesh&, const Mat4f&, const Material& = {}) = 0;
    // setAmbientColor 等の「絵の設定」は ISceneFx へ移動（§6-2、下記参照）。
    // IRenderer3D 自体には後方互換の非仮想転送関数として残っている。
    virtual ISceneFx* sceneFx() noexcept { return nullptr; }
    virtual IExperimentalRenderer3D* experimental() noexcept { return nullptr; }
    static constexpr int kMaxLights = 8;  // setLights() 上限
};
```

### インターフェース構成（§6-2、2026-09）

`IRenderer3D` は元は skybox/shadow/outline/tonemap/splat/Live2D/ニューラル現像まで全部が
同居する巨大インターフェースだった（71〜310 行）。以下の 3 つに分割してある。

| インターフェース | ファイル | 内容 |
|---|---|---|
| `IRenderer3D` | `include/mitiru/render/IRenderer3D.hpp` | frame 制御 (beginFrame/endFrame/resize/finalizeFrame)、camera、light、drawMesh/drawMeshInstanced、culling、drawCallCount、native handle |
| `ISceneFx` | `include/mitiru/render/ISceneFx.hpp` | skybox / ambient / shadow / outline / tonemap / shaderMode の setter・getter（「絵の設定」） |
| `IExperimentalRenderer3D` | `include/mitiru/render/experimental/IExperimentalRenderer3D.hpp` | splat・Live2D・ニューラル現像・relight・clod/glTF モデルなど DX12 限定の実験的機能 |

`IRenderer3D::sceneFx()` / `experimental()` が queryInterface 相当で、対応バックエンド
（`Renderer3D` / `Renderer3D_DX12` / `Renderer3D_WebGL`）は自分自身を返す（多重継承）。未対応
バックエンドは既定で nullptr を返す。`SceneLook::applySceneLook()` は `ISceneFx&` を直接受ける。

既存の `renderer.setTonemapExposure(x)` のような呼び出しは `IRenderer3D` 側に残した非仮想の
転送関数（`sceneFx()`/`experimental()` 経由で委譲）が引き継ぐため、`Screen` 等の既存呼び出し側は
書き換えていない。`Renderer3D` 系の具象クラスは `ISceneFx` / `IExperimentalRenderer3D` を多重継承
するため、自分で override しないメンバは `using ISceneFx::...` 等で明示的に選び、`IRenderer3D` の
転送関数との名前の曖昧さを避けている。

### `Skybox`（`include/mitiru/render/Skybox.hpp`） — DX11 only

```cpp
auto cubemap = Cubemap::verticalGradient(64,
    sgc::Colorf{0.4f, 0.6f, 1.0f, 1.0f},   // 天頂
    sgc::Colorf{0.3f, 0.2f, 0.15f, 1.0f}); // 地表
Skybox sky(cubemap);
sky.initializeDx11(dx11Device);

// 各フレーム
renderer.beginFrame(...);
sky.drawDx11(dx11Context, camera);  // depth=1, write off で背景描画
renderer.setCamera(camera);
renderer.drawMesh(...);
renderer.endFrame();
```

`Skybox` は 6 面 RGBA8 キューブマップを構築するだけの最小版。HDR / IBL / prefilter は別途。

### `Cubemap`（`include/mitiru/render/Cubemap.hpp`） — CPU only

- `Cubemap::solid(size, color)` — 全面同一単色
- `Cubemap::verticalGradient(size, zenith, nadir)` — 天頂↔地表の縦補間、最小実用 sky 用
- `Cubemap::face(CubeFace)` — 1 面アクセス（PosX / NegX / PosY / NegY / PosZ / NegZ）

### `Multi-light`（`include/mitiru/render/LightArrayCB.hpp`）

```cpp
std::array lights{
    Light::directional({0, -1, 0}),
    Light::point({5, 3, 0}, 20.0f),
    Light::spot({0, 10, 0}, {0, -1, 0}, 45.0f, 50.0f),
};
renderer.setUseMultiLight(true);   // Phong PS を multi-light 変種にスワップ
renderer.setLights(lights);        // 最大 8 灯まで（超過は silent truncate）
```

注意:
- `setUseMultiLight(true)` は **Phong モードでのみ** 効く。Toon / Flat / NPR は単一光源のままで本フラグは無視。
- 初回 `setUseMultiLight(true)` で内部的に shader を再コンパイル（数 ms オーバーヘッド）。
- `setLights` の clamp 超過分は警告なく捨てる。

---

### オクルージョンカリング（`OcclusionCuller`、`include/mitiru/render/OcclusionCuller.hpp`）

```cpp
renderer.setOcclusionCullingEnabled(true);   // 既定 OFF
renderer.drawMesh(...);
int skipped = renderer.occludedCount();      // 今フレーム (beginFrame 以降) に隠蔽判定でスキップした累積数
```

CPU 側で Hi-Z ピラミッドを構築し、AABB を直近に読み戻した深度で隠蔽判定する近似カリング。
`drawMesh`/`drawMeshInstanced` は視錐台カリングを通過したメッシュに対して追加でこの判定を行う。
DX11 / DX12 とも既定 OFF、`kOcclusionUpdateInterval`（4）フレームに 1 回だけ深度読み戻しを行う
（毎フレーム読み戻すと GPU 完了待ちのコストが大きいため）。

**DX11 (`Renderer3D`)**: バックバッファが 4x MSAA を採用する GPU では深度バッファも同じ
サンプル数で作られる（`createDepthBuffer()` がバックバッファの `sampleCount()` に合わせる）ため、
単一サンプル前提の `CopyResource` でそのまま staging へ読み戻せない（DX11 に深度専用の
`ResolveSubresource` が無い）。かわりにフルスクリーン ピクセルシェーダーで深度を
`Texture2DMS<float>` として `Load` し、全サンプルの最小値（カメラに最も近い＝最も保守的な
遮蔽物候補）を単一サンプル R32_FLOAT の resolve RT へ書いてから `CopyResource`＋`Map` で読み戻す
（`Renderer3D::resolveMultisampledOcclusionDepth`、シェーダーは
`DefaultShaders3D.hpp` の `OCCLUSION_MIN_DEPTH_RESOLVE_PS_3D`）。resolve 用シェーダーの
コンパイル自体に失敗した環境でのみ `isOcclusionDepthReadbackUnsupported()` が true になり
無効のまま安全に縮退する。

**DX12 (`Renderer3D_DX12`)**: 深度バッファは常に 4x MSAA (`DXGI_FORMAT_R32_TYPELESS`) で
最初から SRV 互換のため、DX11 と同じ min-depth resolve を PS だけ新規に用意して配線した
（`DX12OcclusionResolveShaders.hpp`、VS は既存のフルスクリーン三角形 `OUTLINE_POST_VS` を流用、
深度 SRV も outline post 用に既に確保済みの `m_depthSRVHeap` スロット0を共用）。DX12 は
コマンドリストの実行が `finalizeFrame()` まで遅延するため、resolve→readback（`CopyTextureRegion`
で `D3D12_HEAP_TYPE_READBACK` バッファへ）は `endFrame()` の描画コマンド列に積むだけにとどめ、
実際の `Map` は同じフレームスロットを次に再利用する `beginFrame()`（デバイス側が既にそのスロットの
GPU 完了をフェンス待機済み）の先頭で行う（`consumeOcclusionReadback`）。これにより DX11 のような
毎回の GPU stall を伴わない。

コスト特性は両バックエンドで大きく異なる。DX11 は resolve 描画＋`CopyResource`＋`Map` の GPU 完了
待ちを `endFrame()` 内で同期的に払うため、320x180・4x MSAA 環境で読み戻しが走るフレームは
概ね 1ms 前後、走らないフレームは無視できる水準（詳細な数値は `TestOcclusionCulling.cpp` の
`[.benchmark]` ケース参照）。DX12 は resolve をコマンドリストに積むだけで GPU 実行は
`finalizeFrame()` 後に非同期に走るため、`endFrame()` 自体の CPU コストは resolve の有無に
関わらずごく小さい（詳細は `TestDx12Occlusion.cpp` の `[.benchmark]` ケース参照）。
いずれも `kOcclusionUpdateInterval` フレームに 1 回への間引きで平均負荷は無視できる水準に収まる。

## 既知の制限事項

1. **PBR/IBL は DX12 のみ、簡略化あり** — `ShaderMode3D::PBR` は共有 root signature の SRV slot が埋まっているため専用 root signature/PSO（`include/mitiru/render/dx12/Renderer3D_DX12_PBR.hpp`）を持つ。鏡面 IBL は split-sum（roughness 別 prefilter mip 連鎖 `kPbrPrefilterMipCount`=5 段 + 環境 BRDF 表 t2）。prefilter は CPU 畳み込み（64 サンプル、最下段 4²）なので高周波の環境では粒が出る。シャドウマップ非対応（Toon/Phong 経路の CbShadow/PCF は使わない）。DX11 には PBR が無い。
2. **共通 `Skybox` クラス（`include/mitiru/render/Skybox.hpp`）は DX11 のみ** — `initializeDx12()` は無い。ただし `Renderer3D_DX12` はこのクラスを経由せず、自前の skybox 実装（`include/mitiru/render/dx12/DX12Skybox.hpp`、`setSkybox(Cubemap)`）を持つため、DX12 consumer でも skybox 自体は使える。
3. **SSS は DX11 のみで未配線、DX12 移植は見送り（B15）** — `include/mitiru/render/SubsurfaceScatter.hpp` は単体テスト (`TestSubsurfaceScatter`) はあるが `Renderer3D` の描画パスへは接続されていない。実装が丸ごと D3D11 API（`ID3D11Device`/`ID3D11DeviceContext` 等）直書きのため DX12 へは移植ではなく別実装が要ること、かつどのマテリアルに SSS を適用するかを表すフラグが `Material` にもシェーダーモードにも無く画面全体へブラーをかけると誤りになることの 2 点から、材質フラグ設計を含む別スコープの意思決定が必要と判断し見送った。Tessellation は現状コードベースに実体が無い。SSR は ADR 0070 で DX12 に入った。OIT (Weighted-Blended) は DX12 の `Renderer3D_DX12` に無条件で配線済み、experimental ではない。
4. **影は DX12 のみ GPU 実装、CPU pipeline は別系統** — `DeferredPipeline` 内の `ShadowMap` は CPU 実装（DX11/Vulkan/OpenGL/WebGL 側の GPU shadow は未実装）。DX12 は PCF 付き単一シャドウマップ + 2 カスケード（`ISceneFx::setCascadedShadowEnabled`）まで実装済み。`SceneLook::shadowCascadeAutoFit` (renderer 直なら `shadowConfig().autoFitCascades`) を立てると split・ortho の大きさ・フォーカス点をカメラ視錐台から毎フレーム決める (ADR 0039)。3 カスケード（`setShadowCascadeCount(3)` / `SceneLook::shadowCascadeCount`）は中距離と遠距離を 1 枚の 2 列アトラス（t2）に焼き、root signature は増やしていない。4 段以上は未実装。
5. **`Pipeline3D::flushToGpu()` は Windows のみ** — 非 Windows は `m_drawCallCount` のカウントのみで silent fallback（仕様、`.claude/rules/mitiru-engine.md` の graceful degradation 規約）。
6. **DX12 カスケードシャドウの合わせ方** — カスケードを頼まない (`setCascadedShadowEnabled(false)`、`SceneLook::shadowCascaded` が false) 既定は、
   画質の上限 (`QualityCaps::maxShadowCascades`、既定 3) までの段を視錐台に合わせる (`autoFitCascades` と同じ)。カスケード 0 の影マップは
   2048、カスケード 1・2 は 1024 の 2 列アトラス。比較の余白は影マップの 1 texel の世界の幅に比例させ、点を法線の向きへ 1 texel 浮かせる
   (`DX12SunShadowShaders.hpp` の `shadowLookup`)。level3d の既定のカメラで箱の影を測ると (`TestSunShadowQuality.cpp`、1080p 換算)、
   texel は 3.9 cm (画面の 9.2 画素) から 0.69 cm (1.6 画素) に、縁の幅は 10.2 から 2.2 画素に、縁のギザギザは 2.0 から 0.7 画素になり、
   箱の足元に付く影の割合は 0.74 から 1.0 になった。費用は 1080p の Release で level3d 0.57 → 0.58 ms、outdoor3d は変わらない。
   カスケードを頼んで `autoFitCascades = false` なら `cascadeSplitDistance` を手動設定（既定 15.0）し、カスケード0/1 とも同じ focus 点（前フレーム caster 重心）で ortho half extent だけ変える。`autoFitCascades = true` では `[cameraNear, cascadeMaxDistance]` を実用分割 (λ = `cascadeSplitLambda`、既定 0.7) で 2 分し、各視錐台切片の包含球の半径を half extent、球の中心をそのカスケードの focus にする（`Shadow.hpp` の `frustumSliceBoundingSphere` / `fitCascadesToCamera`）。包含球が 50 より大きければライト視点距離と farClip も球を覆うまで伸ばす。包含球なのでカメラの回転で ortho の大きさは変わらず、平行移動は `snapProjectionToTexels` が ortho の中心をテクセル格子に揃えるので縁がちらつかない。3 カスケード（`setShadowCascadeCount(3)` / `SceneLook::shadowCascadeCount`）は中距離と遠距離を 1 枚の 2 列アトラス（t2）に焼き、root signature は増やしていない。4 段以上は未実装。

---

## ロードマップ（優先順）

### 短期（次回 iteration）

1. **Skybox DX12 path** — `Skybox::initializeDx12()` 追加で DX12 consumer 対応
2. **Multi-light の Toon 派生** — Toon × multi-light の組み合わせ

### 中期

5. **Cascaded shadow map の拡張** — 現状 DX12 の 3 カスケードまで（手動 split または視錐台自動フィット）。4 段以上とカスケード境界のブレンドが残り
6. **Vulkan 3D の最小経路** — native 構成クロスプラットフォーム化の橋頭堡
7. **DX11 の MSAA** — DX12 は resolve 込みで実装済み。DX11 は未着手
8. **SSS の配線** — `SubsurfaceScatter.hpp` を `Renderer3D` の描画パスへ接続（DX12 移植は B15 で見送り済み。着手するならまず「どのマテリアルに適用するか」を表す材質フラグの設計から）

### 長期

9. **Compute particle 経路** — `ParticleCollision` の GPU 実装
10. **Console プラットフォーム（DX12 polish / Vulkan polish）** — SCOPE.md の roadmap
11. ~~IBL の roughness 別 prefilter mip 連鎖 + split-sum BRDF LUT~~ — 2026-09-17 に実装（上表）

---

## テスト戦略

- **CPU データテスト**: 構造体レイアウト / fromLights 等の packing / Cubemap factory — 常時実行
- **DX11 GPU smoke**: `[dx11]` タグ、`D3D11CreateDevice` 成功時のみ動作、headless (ウィンドウを出さない) CI ではスキップ
- **DX12 GPU smoke**: `[dx12]` タグ、`D3D12CreateDevice` 成功時のみ動作
- **Visual Regression (VRT)**: 2D は 5 枚 golden が確立済み（`tests/render/golden/`）、3D は今後追加

新規 3D 機能を追加する場合は必ず:
1. CPU 単体テスト（構造体・API 契約）
2. GPU smoke（リソース作成・シェーダーコンパイルが succeed すること）
3. （可能なら）golden VRT

の 3 階層を網羅すること。`.claude/rules/test-standards.md` も参照。

---

## 履歴

| 日付 | 変更内容 |
|---|---|
| 2026-10-03 | 間接光 (ADR 0070): 焼いた SH L2 の放射照度のプローブ、箱で映す反射のプローブ、前のフレームの HZB を辿る SSR を DX12 に足した。場面の表に t39..t43、CbCluster を 1024 byte に |
| 2026-09-26 | WebGL 列を実コードに合わせて訂正（`Renderer3D_WebGL` が 2026-08-22 に入っていたのに全行「—」のままだった）。凡例に「出荷」を追加。Metal が `feat/macos-support` にあることを追記 |
| 2026-09-16 | 台帳の実装状況が古かった項目を実コードに合わせて訂正: DX12 MSAA resolve / Weighted-Blended OIT は既に無条件配線済み、`AdvancedEffects.hpp` は存在しない、SSR/Tessellation は未着手。`BackendInit.hpp` に `NullRenderer3D`（Vulkan/OpenGL は warnOnce）を追加、DX12 device-lost 検知（`Dx12Device::checkDeviceLost`/`anyDeviceLost`/復旧上限）を追加 |
| 2026-09-14 | Renderer3D_DX12 に視錐台カリング (`Frustum::extractFromCamera`/`isMeshVisible`、`setFrustumCullingEnabled`/`culledCount`) と GPU instancing (`drawMeshInstanced`、`DX12Instancing.hpp`) を配線。multi-light / skybox の DX12 対応表を実装済みに更新（台帳の更新漏れだった） |
| 2026-05-12 | Skybox / Cubemap 追加（DX11）、Multi-light 配線（IRenderer3D::setLights）、AdvancedEffects を experimental/ 隔離、本ドキュメント新規作成 |
| 2026-05-07 | ENG-104: DX12 PixelGrid path、ENG-101: InputState::endTick |
| 2026-05-05 | IRenderer3D::setAmbientColor、native 構成 template |
| 2026-05-04 | Visual inspector overlay、anchored entity 系 |
