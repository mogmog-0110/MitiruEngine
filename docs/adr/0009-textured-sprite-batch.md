# ADR 0009 — テクスチャ付きスプライトのバッチ描画 (per-pixel rect の廃止)

- **Status:** Accepted (2026-05-26). 実装中 (DX12 path 先行)。
- **Context:** エンジンには 2 種類のスプライト描画があったが、どちらも実用的な
  「テクスチャ付きクワッドを多数」描く用途に不適だった:
  - `Screen::drawSprite(tex, dst[, srcRect, tint, flipX])` — バッチ＆カメラ変換は
    正しいが、実装が **1 ソースピクセル = 1 `emitRect`**（`detail/Screen_Sprites.hpp`）。
    32×32 チップ = 最大 1024 rect/枚。tilemap で数十〜数百タイル描くと
    **数万 rect/frame** になり fps が落ちる。
  - `Screen::drawPixelGrid` — 1 クワッド/コールで速いが (a) `currentTransform`
    無視、(b) 即時・非バッチ、(c) 単一スクラッチテクスチャに毎フレーム再アップ
    ロード。複数チップを同時に扱えない。
  - `render::SpriteBatch::drawSprite(textureId, …)` は `textureId` が
    `static_cast<void>` で捨てられ、per-sprite テクスチャサンプリングが未配線。

  一方で、**GPU 側の能力は既に揃っていた**: DX12 base 2D root signature は
  SRV テーブル(t0) ＋ static sampler(s0) を持ち、`DEFAULT_PS_2D` は cbuffer の
  `uUseTexture` が非 0 なら t0 をサンプルする。`submitBatchDx12` が null SRV ＋
  `uUseTexture=0` を流していただけ。さらに submit は **1 コール = 1 コマンド
  リスト + fence** で、`present()` は frame 内で複数回 submit する多パス設計
  （shapes → SDF → pixelgrid）。

  ＝「テクスチャごとの submit を順序を保ったまま差し込む」だけで、shader / PSO /
  root signature を一切改造せずに本物のテクスチャ付きバッチ描画が実現できる。
  これは 5 軸②③④を支える描画基盤の parity 作業であり、ドット絵ゲームの
  tilemap / sprite-sheet 本命描画に必要。

- **ABI:** **C ABI (ModuleApi の POD: InputSnapshot/FrameIntents) には触れない。**
  変更は engine 内部の header-only な `Screen` / `RenderPipeline2D` /
  `SpriteBatch` に閉じる (consumer DLL は engine と一緒に再コンパイルされる)。
  `kCurrentApiVersion` の bump は不要。

## 決定

**`Screen::drawSprite` を「本物のテクスチャ付きクワッド」を texture-keyed バッチに
emit するよう作り替え、per-pixel rect ハックを廃止する。** z-order は
**submit-on-switch** で保つ。

### 1. submit-on-switch（順序保持 ＋ バッチ合流）

`present()` / `pushClipRect()` が既に行っている「frame 内 mid-frame submit」を
スプライトにも適用する。同一 RTV へ painter 順で上書きするので順序は保たれる。

- `Screen` は「現在のバッチ」とその `texHandle`（0 = 頂点カラー / null SRV）を持つ。
- 頂点カラー emit (`emitRect` / `emitGradientRect`) は、開いている textured run が
  あれば flush+submit してから書く。
- textured `drawSprite(tex,…)` は、現在の run と texHandle が違えば flush+submit
  してから書く。
- 連続する同一テクスチャのスプライトは 1 つの run = **1 ドローコール**に合流する
  （tileset 1 枚から 200 タイル → 1 submit / 200 quad）。
- 純 shape のみの frame では textured run が一度も開かないので mid-frame submit は
  発生せず、従来と同じ単一 submit のまま（hot-path 退行なし）。

### 2. 永続テクスチャ・キャッシュ（DX12）

`RenderPipeline2D` に `render::Texture` → GPU テクスチャ + SRV のキャッシュを持つ。
キーは `const void*`（`&Texture`）＋ (w,h) ガード。アップロードは初回のみ
（pixelgrid の CopyTextureRegion + COPY_DEST↔PSR barrier を流用）、以降は SRV を
再バインドするだけ。pixel-art の鮮鋭さのため point-filter PSO variant を優先。

### 3. graceful degradation

textured-batch は **DX12 path のみ**実装する。それ以外（software framebuffer /
DX11 / WebGL / Null）では `drawSprite` は従来の per-pixel `emitRect` 経路に
fallback する。＝ headless テスト・ソフトラスタライザ・他 backend は無傷。
DX11/WebGL への textured-batch 配線は後続作業。

## 検討した代替案

- **テクスチャアトラス / 複数テクスチャを 1 バッチに**: bindless or atlas で
  per-sprite テクスチャを 1 ドローコールに混ぜる。最速だが root signature /
  shader 改造が要り、scope が大きい。submit-on-switch で「テクスチャ種類数 ≒
  ドローコール数」は実ゲーム（tileset 数枚）で十分速い。将来必要なら拡張する。
- **`drawPixelGrid` を transform 対応 ＋ 永続ハンドル化**: 小さいが per-sprite
  バッチにならず、drawSprite の per-pixel ハックも残る。中途半端なので却下。

## 失敗モード分析

| 失敗モード | 対策 |
|---|---|
| `&Texture` キーが別テクスチャに再利用される (free→realloc) | (w,h) ガード ＋ 寸法不一致時は同スロットに再アップロード。一時テクスチャ多発は cache 肥大 → 件数上限で古いものを退避 |
| `uUseTexture` が後続の頂点カラー submit に漏れる | 各 submit は冒頭で `waitDx12Fence` 後に PS CB を明示設定（textured=1 / 頂点カラー=0）。race フリー |
| software framebuffer がテクスチャをサンプルできない | DX12 path 以外は per-pixel fallback。textured run は開かず mid-frame flush も起きない |
| mid-frame submit 増による CPU stall | shape のみ frame では発生しない。スプライト使用時のみ「テクスチャ種類数 + 切替回数」分だけ。per-pixel 数万 rect より遥かに安い |

## 5 軸との関係

② time-travel inspector / ④ deterministic replay は GameMemory が唯一の state な
構造に依存し、本変更は描画のみ（state を持たない）なので不変条件を侵さない。
ドット絵ゲームを「実際に出せる」品質に引き上げ、demo (軸①の HTML UI とは別の
native 描画品質) を底上げする。
