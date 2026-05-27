# ADR 0008 — 音の再生は FrameIntents intent 経由 (DLL ゲームから既存 mixer へ)

- **Status:** Accepted (2026-05-25). 実装中。
- **Context:** エンジンは完全な audio スタックを持つ (`include/mitiru/audio/`:
  `AudioMixer`〔BGM/SE/Voice カテゴリ〕, `MiniaudioBridge`, FMOD backend,
  `MitiruAudioPlayer`)。にもかかわらず **DLL ゲームから音を鳴らす経路が無い**:
  ADR 0005 で game は `Engine*` / mixer pointer を持てず、`FrameIntents` には
  sound 用フィールドが存在しない。結果、「他のエンジンなら当然できる『当たった
  ら効果音』」が現状の DLL モデルでは不可能。これは feature 追加というより
  **既存能力を ADR 0005 境界に接続する parity 作業**。
- **Extends:** ADR 0005 (Host-Game C-only signal flow)。本 ADR は signal-only
  原則を audio に適用する一事例。
- **ABI:** `FrameIntents` にフィールドを追加するため **ABI break**。
  `kCurrentApiVersion` を 3 → 4 に bump する (CLAUDE.md「境界 struct の
  フィールド追加は ABI break」ルール準拠)。**1.0 前に消化する** (1.0 後の
  ABI break は安定宣言違反になるため)。

## 決定

**ゲームは「この音を鳴らして」という intent を `FrameIntents` に書くだけ。host が
所有する mixer がそれを実行する。** ゲームは mixer も AudioEngine pointer も
持たない (ADR 0005 不変条件 1)。

### 境界 struct (POD)

```cpp
struct SoundIntent
{
    char         id[64];     ///< 論理サウンド id (例: "hit", "bgm_battle")。null 終端。
    std::uint8_t category;   ///< 0=SE, 1=BGM, 2=Voice (mitiru::audio::SoundCategory)
    std::uint8_t loop;       ///< 1 = ループ (BGM 用)
    std::uint8_t stop;       ///< 1 = 「鳴らす」でなく「この category/id を止める」
    std::uint8_t _pad;
    float        volume;     ///< 0.0–1.0
};

// FrameIntents の末尾に追記 (既存フィールドのオフセットを変えない):
std::int32_t soundIntentCount;
SoundIntent  soundIntents[8];   ///< 1 フレーム最大 8 件
```

- 末尾追記なので既存フィールド (statePushes 等) のオフセットは不変。だが struct
  サイズが変わる = memcpy 境界が変わるため **ABI version は必ず bump**。

### サウンド id の解決 (host 側)

- host は起動時にゲームの `assets/audio/` 配下の音声ファイル
  (`*.wav` / `*.ogg`) を **拡張子を除いたファイル名を id** として mixer に
  ロードする (例: `assets/audio/hit.wav` → id `"hit"`)。
- ゲームは `intents.soundIntents[n].id = "hit"` と書くだけ。manifest 不要の
  規約ベース (アトミック哲学: 設定より規約)。将来 manifest が要れば JSON で
  足す (純データは JSON ルール)。

### host の処理 (毎フレーム頭)

```
for i in [0, soundIntentCount):
    si = soundIntents[i]
    if si.stop: mixer.stop(category, id)
    else:       mixer.play(si.id, si.category, si.loop, si.volume)
```

device 不在時は `NullAudioEngine` で no-op (engine の graceful degradation ルール)。

## なぜ — 失敗モード分析

| 失敗モード | 起因 | 本設計の対処 |
|---|---|---|
| game が mixer pointer を保持し、unload 後に dangling | 境界跨ぎ pointer | **保持させない**。intent (POD) のみ。ADR 0005 と同型 |
| 毎フレーム同じ音を投げて多重再生 (押しっぱなしキー) | edge 判定漏れ | 「いつ鳴らすか」は game ロジックの責任 (`keysJustPressed` 等)。host は `soundIntents[8]` で 1 フレーム上限を構造的にcap |
| id typo → 無音 | ゆるい id 解決 | host が未知 id を dev ログに警告 (silent fail を可視化)。将来 `mitiru lint` 的な audio-manifest 照合も可能 |
| ABI: v3 module を新 host に load → struct サイズ不一致で破損 | フィールド追加 | host が `api->version` を見て、v3 module には soundIntents を**読まない** (= 音無しで動く)。crash させない |
| 決定論 / replay が音で壊れる | side-effect 混入 | **壊れない**。音は出力 (FrameIntents) であり入力ではない。Recorder は InputSnapshot のみ記録するので audio は再生結果に影響しない。リプレイは同じ intent を再生成するが、音の有無は state に無関係 |
| headless / CI で音デバイス無し | 環境差 | `NullAudioEngine` で intent は no-op。テストは intent が**生成されたか**を assert (実際の発音は不要) |

## 5 軸との整合

軸① (HTML/CSS UI in C++) を直接強化はしないが、**ゲームを「他エンジン並みに完成」
させる parity の第一歩**。同じパターン (既存能力を intent 経由で DLL に開放) は
今後 particle / scene transition 等にも一般化できる (本 ADR がその雛形)。

## 検証計画

- **engine test (solo)**: `FrameIntents` に soundIntents を書く helper +
  host 消費ロジックの unit test (mixer mock で「play が呼ばれたか」を assert)。
- **NullAudio path (solo)**: device 無しで intent が no-op になることを確認。
- **実機 (ユーザーの耳)**: hello_game で被弾時に `hit` を鳴らし、実際に聞こえるか。
  ABI bump 後、hello_game / spire_crawl / templates / mitiru_host を全リビルド。

## 代替案 (却下)

- **game が直接 mixer を呼ぶ** → ADR 0005 違反 (Engine pointer 保持)。却下。
- **JS から音を鳴らす (CEF audio)** → zero-JS 哲学 (ADR 0007) と state ownership
  違反。却下。
- **音 id を int enum で渡す** → game ごとに音が違うので enum は固定できない。
  文字列 id + 規約ロードを採用。
