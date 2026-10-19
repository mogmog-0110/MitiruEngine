# トゥーンの金属

トゥーンの絵の中で、金属らしい質感を出す機能 (ABI v57、ADR 0083)。`Screen::toonStyle3D` に渡す `ToonStyle` の `metal` に値を書くと、以後の描画が金属の見た目になる。
`ToonStyle::bands` が 1 以上の描画にだけ効く。`bands` が 0 のときと、`drawModel` で描く .clod の物には効かない。

## 何を足したか

PBR の金属 (環境マップの反射とフレネル) をそのまま入れず、アニメ調の金属の描き方に合わせた。描き方は 3 つの要素でできている。

- 環境を映すマットキャップ。反射ベクトル R = reflect(-V, N) の y から、空・地平・地面の 3 色のグラデーションを引き、`bands` 段に刻む。地平は境を鋭くして、空と地面を 1 本の線で分ける
- 反射の色は塗った色で染める。金属は反射が塗った色を持つため。割合は `tint`
- 段付きのつや (`glossMinNdotH`) と縁の帯 (`rimMaxNdotV`) は、金属の上にそのまま重ねる。段の数・境・影の側の色の落とし方も、トゥーンの設定が決める

`amount` が 1 に近いほど拡散を反射に置き換える。環境光と局所光の拡散も `1 - amount` 倍になる。`amount` が 0 の描画は、金属を足す前と同じ絵になる。

## 参考にした作品と資料

アニメ調の金属は、環境を映す絵をテクスチャ (マットキャップ) で貼り、つやと縁の光を重ねて作る例が多い。

- マットキャップの考え方の出どころ: P.-P. Sloan, W. Martin, A. Gooch, B. Gooch, "The Lit Sphere: A Model for Capturing NPR Shading from Art", Graphics Interface 2001, pp. 143-150 (<https://users.cs.northwestern.edu/~bgooch/PDFs/LitSphere.pdf>)。球に描いた絵を、視点から見た法線で引いて陰影にする
- 実装の手本: lilToon (MIT License、<https://github.com/lilxyzw/lilToon>)。MatCap を最大 2 枚持ち、通常・加算・乗算・スクリーンで混ぜられる。コードは取り込まず、考え方だけ参考にした
- トゥーン調の金属を、反射マップやマットキャップで偽装する作り方: BlenderNation "Faking Toon Metallic Reflections" (<https://www.blendernation.com/2021/05/28/faking-toon-metallic-reflections/>)
- 原神風のシェーダーが金属用のマットキャップを持つ例: Ben Ayers, "Recreating the Genshin Impact Shader" (<https://bjayers.artstation.com/blog/category/5>)

マットキャップのテクスチャ (画像) は使っていない。画像を足すと、ゲームから画像を登録する口が要る。3 色の勾配を式で書けば、空の色をゲームの空に合わせるのも JSON の数値で済む。
ここで「段に刻む」のは、トゥーンの段 (ADR 0042) と同じ見た目に揃えるための選択で、出典の作品が段に刻んでいるわけではない。

## OSS を使わなかった理由

lilToon と UnityChanToonShader は Unity のシェーダーで、MitiruEngine の描画 (DX12 の PS) には移せない。MIT License なので考え方の参考にはできるが、コードは使えない。
車輪の再発明を避ける方針には、エンジンが既に持つ段 (`toonBandLevel`)、縁の帯、つや、`linearRgb` の色の約束をそのまま使うことで合わせた。足したのは反射の式 1 つ (`toonMetalEnv`) と定数だけ。

## 使い方

```cpp
mitiru::render::ToonStyle style;          // 段・線は今までどおり
style.bands = 2;
style.metal.amount = 0.9f;                // 0 で金属なし
style.metal.tint = 0.7f;                  // 反射を塗った色で染める割合
style.metal.shadowFollow = 0.5f;          // 影の側で反射を暗くする割合
style.metal.bands = 4;                    // 環境のグラデーションを刻む段数 (0 は滑らか)
style.metal.horizonHeight = 0.1f;         // 反射ベクトルの y で空と地面を分ける高さ
style.metal.horizonSoft = 0.03f;          // 地平の境の幅
// sky / horizon / ground は sRGB の 0..1
s.toonStyle3D(&style);
// 金属にしない描画の前に toonStyle3D(&別の style) か nullptr
```

`ToonStyle` は 224 byte の POD (160 byte の本体と、末尾の 64 byte の `ToonMetal`)。Screen のメンバと関数は増えない。
金属の指定は `bands`・線の指定とは別に、描画ごとの定数 (`CbDrawEx`、256 → 512 byte) に載る。

## 向かないところ

- 反射は視点と法線だけで決まる。周りの物は映らない (街の建物が球に映ることはない)。空の色は `sky` `horizon` `ground` で手で合わせる
- Phong・PBR の描画、clod には効かない
