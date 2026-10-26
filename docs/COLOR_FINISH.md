# 色の仕上げ (LUT、ビネット、フィルムグレイン)

ゲームごとの色の調子を、シェーダーでも C++ でもなく画像と JSON で持つための機能です。3D の絵の最後に、色の変換 (LUT)、画面の端の暗さ (ビネット)、粒 (フィルムグレイン) を掛けます。DX12 と Dawn のどちらでも同じ式で動きます。

## 使い方

1. 宣言を `assets/color_looks.json` に書きます。`looks` の並びが番号 (1 始まり) になります。

```json
{
  "looks": [
    { "name": "warm", "lut": "luts/warm.png", "strength": 1.0 },
    { "name": "film", "lut": "luts/teal_orange.png", "strength": 0.8,
      "vignette": { "strength": 0.45, "radius": 0.35 },
      "grain": { "strength": 0.05, "cell": 2 } }
  ]
}
```

2. ゲームの `draw` で、`SceneLook::colorLut` に番号を入れて `sceneLook3D` を呼びます。0 は仕上げなしで、今までと同じ絵です。

```cpp
mitiru::render::SceneLook look;
look.colorLut = 2;   // film
s.sceneLook3D(look);
```

| 項目 | 範囲 | 意味 |
|---|---|---|
| `lut` | 宣言の隣からの相対パス | LUT の PNG。省くとビネットとグレインだけを掛ける |
| `strength` | 0 から 1 | 元の色と LUT を通した色の混ぜ具合 |
| `vignette.strength` | 0 から 1 | 隅の暗さ。0 でなし |
| `vignette.radius` | 0 から 0.99 | 中心から隅までを 1 として、暗くし始める位置 |
| `grain.strength` | 0 から 0.25 | 粒の振れ幅 (色の値) |
| `grain.cell` | 1 から 16 | 粒の 1 辺 (出力の画素)。4K では 2 から 3 にする |

宣言の場所は `EngineConfig::colorLooks3D` で替えられます。ファイルが無いゲームには、何も確保しません。番号が宣言の数を超えるか、宣言が無いときは、1 度だけ知らせて絵は変えません。

## LUT の画像

一辺 N (2 から 64) の立方体を、幅 N*N・高さ N の PNG に並べたものです。入力 (r, g, b) の出力は画素 (x = b*N + r, y = g) に入ります。値は sRGB に符号化したままです。恒等 LUT は、N が 16 なら 8 bit の格子の値が割り切れて、どの色も変えません (N が 32 だと最大 1 階調ずれます)。

`.cube` からの変換と、見本の LUT は `tools/cube_to_lut_png.py` で作ります。

```
python tools/cube_to_lut_png.py in.cube out.png [--size N]
python tools/cube_to_lut_png.py --preset warm|mono|teal-orange|swap --size 16 out.png
python tools/cube_to_lut_png.py --identity 16 out.png
```

`.cube` は赤が最も速く変わる並びとして読みます (Adobe の仕様書そのものは取れず、二次資料で確かめました)。

## 掛かる場所と式

3D の後処理の最後、AA とアップスケールを済ませた sRGB 符号化済みの LDR に掛けます。順は LUT、ビネット、グレインです。当たりの演出と止め絵、HUD の前なので、これらには掛かりません。輪郭線と AA の後なので、輪郭線にも掛かり、粒は AA にならされません。副ビュー (分割画面・小窓) には掛かりません。

- LUT は 8 隅を整数座標で読んで自前でトライリニアに補間します。ハードウェアの補間は GPU で精度が違い、2D に並べた画像では隣の段へにじむためです。
- 粒は整数のハッシュで、シードは決定論のフレーム番号 (`Engine::frameNumber`) です。同じスクリプトは同じ粒になります。描いた回数は使いません。
- HDR 出力のときは、LUT が掛かるのは SDR の絵 (0 から 1) だけです。表示のピークまで開いた明部は素通しです ([HDR_OUTPUT.md](HDR_OUTPUT.md))。

式は `include/mitiru/render/ColorFinishMath.hpp` に CPU 実装があり、試験は GPU の読み戻しをこれと比べます。

## 費用

使わないフレームは、パスもコピーも作りません。使うと、出力と同じ大きさの RGBA8 のコピー 1 枚 (当たりの演出と共有) と、全画面の 1 パスが増えます。`PostGpuPass::ColorFinish` で GPU 時間を測れます。

## Dawn

DX12 と同じ式の WGSL (`shaders/dawn/colorfinish.slang`) で動きます。互換レベルでも使えます。
