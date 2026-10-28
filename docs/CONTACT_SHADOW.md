# 接触影

小さな物の足元に、短い影を足す機能です。太陽の影 (cascade) の解像度では出ない接地感を、画面の深度だけから作ります。既定は切で、入れなければ今までと同じ絵です。DX12 だけで効き、Dawn は影を足しません。

## 使い方

1. `mitiru.toml` の `[render] needs` か `MITIRU_RENDER_NEEDS` に `contact_shadow` を足します。AO のテクスチャと深度の表を共用するので、`ambient_occlusion` も一緒に宣言されたことになります。
2. ゲームの `draw` で、`SceneLook::contactShadow` に濃さを入れて `sceneLook3D` を呼びます。

```cpp
mitiru::render::SceneLook look;
look.contactShadow = 0.7f;
look.contactShadowDistance = 0.6f;
s.sceneLook3D(look);
```

| 項目 | 範囲 | 意味 |
|---|---|---|
| `contactShadow` | 0 から 1 | 影の濃さ。0 で切 |
| `contactShadowDistance` | 0 以上 | 光の向きにレイを進める長さ (ワールド単位)。0 は 0.5 |

光の向きは `setShadowDirection` (`SceneLook::shadowDirection`) と同じです。

## 仕組み

AO の直後に 1 パス足します。各画素の視空間の位置から光の向きに 24 歩進め、歩ごとに画面へ投影して深度を読みます。レイの点より手前に面があり、その差が厚み (レイの長さの半分) 以内なら、遮られたとして数えます。遮られた位置が始点に近いほど濃く、遠いほど薄くなります。結果は AO のテクスチャへ掛けて、tonemap が HDR の色に乗算します。AO が切のフレームでも使えます。

限りのあること。

- 画質の段が AO を落とす設定のとき (`QualityCaps` の `ambientOcclusion` が偽) は、接触影も描きません。AO を `setAmbientOcclusion(false)` で切っているだけなら描きます。
- 光に背を向けた面 (深度から組み直した面の向きと光の向きの内積が 0 以下) には影を付けません。そこには遮る直接光がなく、レイが自分の面の内側へ入って全部が影になるためです。
- 画面に映っていない物は遮りません。レイが画面の外へ出たら止めます。
- 厚みを超えて奥にある面は遮りません。背の高い物の影は太陽の影が受け持ちます。
- 深度は MSAA の sample 0 を読みます。影は HDR の色に乗せるので、TAA は色の一部として扱います (影だけの履歴は持ちません)。
- トゥーンの硬い影には効果が小さく、PBR の場面で効きます。

## 費用

使わないフレームは何も記録しません。使うと、全画面の 1 パス (24 歩の深度の読み) が増えます。

## Dawn

描きません。機能表の `contact_shadow` は Dawn を「なし」とし、頼まれた回数を `--render-features` の知らせに数えます。Dawn の互換レベルは深度を読めません (ADR 0087)。

## 実機で見る手順

`mitiru_host.exe cave_exposure/cave_exposure.dll` で、外の岩の足元に短い影が付くことを見ます。
