# モーフターゲット (表情)

glTF の morph target を、ゲームが持つ重みで毎フレーム足して描く。表情、口の形、まばたきがこれで動く。例は [`examples/face3d`](../examples/face3d/face3d_dll.cpp)。

## 使い方

```cpp
const anim::AnimAsset kFace = anim::loadAnimAssetFile("face.glb").value_or(anim::AnimAsset{});   // 骨格と同じ読み込み関数。隣の face.expressions.json も読む

anim::ExpressionPose pose;                       // 名前と 0..1 の重み。136 byte の POD で、GameMemory に置ける
anim::setExpression(pose, "happy", 0.8f);
anim::applyViseme(pose, kHello, talkSec);        // 口の形の時刻表 (後述)

float weights[anim::kMorphMaxSlots];
const int count = anim::evaluateMorphWeights(kFace, &animPose /* 無ければ nullptr */, &pose, weights);
s.drawModelPose("face.glb", world, &animPose, nullptr, 0, render::DrawTint{}, weights, count);
```

`weights` はモデルのスロット表 (`kFace.morph`) の並び。スロットは glTF の nodes の順に、モーフを持つ mesh を参照するノードごとに連番で振る。
host も DLL も同じ glTF から同じ表を組むので、番号が食い違わない。渡した個数が表より多い分は捨て、少ない分は 0。
個数 0 で渡すと `drawModelPose` と同じ描画で、glTF の既定の重みも当てない。重みは -16..16 に丸め、NaN は 0 にする。

## 重みの決まり方 (`evaluateMorphWeights`)

1. 既定の重み (`node.weights`、無ければ `mesh.weights`、無ければ 0)
2. `AnimPoseParams` のレイヤを並べた順に、クリップの weights チャンネルを weight とマスクで混ぜる (姿勢の Override と同じ。加算レイヤは効かない)
   クリップの長さは T/R/S のチャンネルだけから決まる (weights が T/R/S より長いクリップは、その長さで折り返す)。T/R/S を持たない weights だけのクリップは、既存のクリップの番号がずれないよう、いちばん後ろに並べる
3. 表情を足す。VRM 1.0 の `expressions` と同じ規則: 値は 0..1、`isBinary` は 0.5 を超えれば 1、`overrideMouth` / `overrideBlink` / `overrideLookAt` の block と blend、バインドの weight を掛けてスロットごとに足し、0..1 に丸める。表情の名前に定義が無ければ、その名前のモーフ (`extras.targetNames`) を直接動かす

隠れた状態を持たない純関数で、加減乗除と比較だけを使うので、同じ入力から同じビットが出る。入力 (`ExpressionPose`、`AnimPoseParams`、時刻) を GameMemory に置けば、巻き戻しとリプレイで表情も戻る。

評価するスロットは先頭の `kMorphMaxSlots` (128) 個まで。手元の VRM (アリシア) は 60 個、顔の mesh だけなら 49 個。

## 表情の定義 `<model>.expressions.json`

VRM 1.0 の `VRMC_vrm.expressions` と同じ形 (`preset` と `custom`)。モーフのバインド、`isBinary`、override だけを読み、`materialColorBinds` と `textureTransformBinds` は読まない。
`node` は glTF の nodes の番号、`index` はそのノードの mesh の何番目のモーフか。ルートにそのまま書いても、`expressions` の下でも、glTF の拡張 (`extensions.VRMC_vrm.expressions`) を貼っても読める。
`custom` の表情を口・まばたき・視線として打ち消される側に回すときは `"procedural": "mouth" | "blink" | "lookAt"` を足す (VRM にない拡張)。
VRM のファイルを直に読む処理は無い。バインドの先が無い表情は、読み込みの警告に出る。

## 口の形の時刻表 `<name>.visemes.json`

実行中に音を解析しない (音のスレッドの都合でリプレイが崩れるため)。前もって `tools/bake_visemes.py voice.wav -o voice.visemes.json` で焼き、ゲームは音を鳴らし始めてからの秒で `applyViseme` を引く。
書式は `{"keys": [{"t": 0.0, "aa": 0.0, "ih": 0.0, "ou": 0.0, "ee": 0.0, "oh": 0.0}, ...]}`。キーの間は線形で、最初のキーより前と最後のキーより後は口を閉じる。
焼くツールは母音の響き (LPC の F1 / F2) の近さで 5 母音に配る粗い物なので、出来た表は手で直してよい。

## 描き方

- DX12 と Dawn (通常、互換) の compute が、頂点ごとに自分の行 (動くモーフだけ) を target の昇順に足してからスキンする。atomic が無く、結果は実行ごとに同じ。重みが 0 の描画は今までの変形のまま (絵も作業量も同じ)
- 骨の無い mesh のモーフは、重みが 0 でない間だけ compute を通す。0 の間は静的な mesh で描く
- LOD の段は、残る頂点だけに詰め直した差分を持つ。包含箱は、重みの絶対値 × モーフごとの最大の動きだけ広げる
- Dawn で compute を使えないとき、または互換の storage buffer が足りないときは CPU で足す (`render/MorphTargets.hpp` の `morphSkinVertices`)
- FBX の blend shape は、チャンネルごとにいちばん強いキーの形をモーフにして glb へ書く (中間のキーとチャンネルの重みのアニメは読まない)

機能表の行は `morph_targets` (ADR 0090)。
