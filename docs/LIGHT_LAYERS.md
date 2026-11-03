# 光のレイヤー

どの光がどの物に当たるかを、32 bit のレイヤーで絞ります。主人公だけに当てる光、部屋ごとに分ける光、見せたい物だけを浮かせる光に使います。

## 使い方

```cpp
constexpr std::uint32_t kHero = 1u << 0;
constexpr std::uint32_t kRoom = 1u << 1;

// 物の側: 以後に描く物が属するレイヤー。フレームの頭で 0 に戻るので、毎フレーム宣言します
s.lightLayers3D(kHero);
s.drawMesh("cube", hero, ...);
s.lightLayers3D(kRoom);
s.drawMesh("cube", wall, ...);

// 光の側: この光が当たる物のレイヤー
auto lamp = mitiru::render::LocalLight::point(pos, 8.0f, sgc::Colorf{1, 0.9f, 0.7f, 1}, 4.0f);
lamp.layers = kHero;          // 主人公だけを照らす
s.localLights3D(&lamp, 1);
```

## 規則

| 物の layers | 光の layers | 結果 |
|---|---|---|
| 0 | 何でも | 当たる |
| 何でも | 0 | 当たる |
| 0 でない | 0 でない | `物 & 光` が 0 でなければ当たる |

0 は「全部」です。何も宣言しないゲームの絵と費用は変わりません。0xFFFFFFFF を宣言しても、宣言しないときと画素まで同じです。
太陽 (`light3D`) は対象外で、レイヤーに関わらず全部の物に当たります。

## 影

影も同じ規則です。光が照らさない物は、その光の影を受けず、その光の影を落としもしません。

- 受ける側は、光が当たらない画素に影を引く意味がないので、自然にこうなります
- 落とす側は、スポットと点光源 (`castShadow = 1`) の影の深度パスが、光のレイヤーと重ならない物を描きません。Unity の URP が既定でこうしています (Custom Shadow Layers を切っている間)
- 太陽の影はレイヤーを見ません。太陽はレイヤーを持たないためです

## 描画先ごとの違い

機能表 (`light_layers`) は DX12、Dawn 通常、Dawn 互換のどれも「対応」です。

| 描画 | 絞られるか |
|---|---|
| DX12 の前方描画 (トゥーン、Phong、PBR、半透明、自作の材質) | 絞られる |
| DX12 のスポットと点光源の影の caster | 絞られる |
| Dawn の局所光 (通常と互換) | 絞られる。Dawn に局所光の影は無い |
| clod の物 | 絞られない。全部の光を受ける |
| 粒、体積フォグの散乱、デカール | 絞られない |

## 費用

光の側は、見える光ごとに 4 byte (`CbCluster` の末尾、最大 256 灯で 1 KB)。物の側は描画ごとの定数に 4 byte。
画素の側は、froxel から取り出した光ごとに、レイヤーの比較 1 回が増えるだけです。物のレイヤーが変わるたびに、連続する描画のまとめ (draw run、Dawn の instanced draw) は区切られます。

## 出どころ

- Unity: Light Layers (Rendering Layers)。`(光の layers & 物の layers) != 0` で当たり、既定は全部、影は既定で同じ規則に従う
- Unreal Engine: Lighting Channels。光と物が共通のチャンネルを持つときだけ当たる。チャンネルは 3 本で、半透明やマスクの材質には効かない

MitiruEngine は 32 本で、影の caster にも同じ規則を使います。決めた理由は ADR 0122 にあります。
