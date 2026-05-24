# MitiruMML - Claude Configuration

## Project Overview
MitiruMML は YM2608 (OPNA) FM音源エミュレーションベースの MML 音楽エンジン。
MitiruEngine の git submodule として `external/mml/` に配置される。

## 依存関係
```
MitiruEngine (親)
├── external/sgc/  ← ShiggyGameCore (submodule)
└── external/mml/  ← MitiruMML (このプロジェクト, submodule)
    └── external/ymfm/ ← ymfm YM2608エミュレータ (submodule, BSD-3)
```

- **上位**: MitiruEngine が依存する（MitiruMML は MitiruEngine に依存しない）
- **下位**: ymfm (YM2608チップエミュレーション)
- **SGC**: ShiggyGameCore には依存しない（完全独立）

## Language & Standard
- **C++20** / header-only + ymfm static library
- コンパイラ: MSVC 2022, GCC 12+, Clang 15+

## Build
```bash
cmake --preset default
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

## Directory Structure
```
MitiruMML/
├── include/mitiru_mml/
│   ├── MmlTypes.hpp         — 共通型定義
│   ├── MmlParser.hpp        — MMLテキストパーサー
│   ├── Synthesizer.hpp      — ソフトウェア波形合成（旧方式、後方互換）
│   ├── Track.hpp            — トラック状態管理
│   ├── Sequencer.hpp        — ソフトウェアシーケンサー（旧方式）
│   ├── OpnaDriver.hpp       — ymfm YM2608ドライバー（FMドラム合成含む）
│   ├── OpnaPresets.hpp      — FM音色プリセット12種（Furnace TFI実データ）
│   ├── OpnaSequencer.hpp    — OPNAシーケンサー（メイン再生エンジン）
│   ├── WavWriter.hpp        — PCM→WAV変換
│   ├── AudioOutput.hpp      — Win32 PlaySound再生
│   ├── MusicTheory.hpp      — スケール/コード/進行
│   ├── PatternLibrary.hpp   — ドラム/ベース/アルペジオパターン
│   ├── PhraseDictionary.hpp — 旋律フレーズ辞書（46フレーズ）
│   ├── PhraseComposer.hpp   — フレーズベース自動作曲
│   ├── SongBuilder.hpp      — 宣言的楽曲構築
│   ├── MusicPrompt.hpp      — AI向け音楽プロンプト
│   ├── AiComposer.hpp       — AI作曲支援フレームワーク
│   ├── MotifEngine.hpp      — モチーフ変奏エンジン
│   ├── MmlValidator.hpp     — トラック長検証
│   ├── TfiImporter.hpp      — TFI音色ファイルインポート
│   └── MitiruMML.hpp        — アンブレラヘッダー
├── external/ymfm/           — YM2608エミュレータ（submodule）
├── tests/TestMml.cpp         — テスト（156ケース）
└── MML_REFERENCE.md          — Claude作曲リファレンス
```

## MML文法（主要コマンド）
- 音符: `C D E F G A B` / シャープ `C+` / フラット `C-`
- 音長: `C4`=四分, `C8`=八分, `C4.`=付点
- 制御: `T120`=テンポ, `O4`=オクターブ, `L8`=デフォルト音長, `V12`=音量
- FM音色: `@FM0`〜`@FM11` (Piano/Bell/Brass/Strings/Organ/E.Piano/Bass/Flute/Koto/SynLead/Vibes/DistGt)
- SSG: `@SSG`, リズム: `@RHYTHM` (B=キック,S=スネア,H=ハイハット)
- パン: `P0`=左, `P1`=中央, `P2`=右
- ループ: `[CDEF]4`, ループポイント: `$`

## Known Issues
- ADPCM ROMデータ未搭載（FMドラム合成で代替）
- SSGハードウェアエンベロープの活用が限定的
- 自動作曲(PhraseComposer)は品質に限界あり。Claude手書きMMLを推奨

## Coding Standards
- CLAUDE.md は ShiggyGameCore の規則に準拠
- ファイル: UTF-8 BOM付き
- インデント: タブ
- ブレース: Allman style
- コメント: 日本語Doxygen
- TEST_CASE名: ASCII限定
