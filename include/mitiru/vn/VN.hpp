#pragma once

/// @file VN.hpp
/// @brief ビジュアルノベルモジュール マスターインクルードヘッダ
/// @details MitiruEngine VNモジュールの全ヘッダを依存順に一括インクルードする。
///
/// 含まれるモジュール:
///   - EasingFunctions     : イージング関数ライブラリ
///   - TrueTypeFont        : stb_truetypeベースTTFレンダラー
///   - NineSlice           : 9スライスUIスプライト
///   - TextAnimator        : テキスト表示アニメーション
///   - RubyText            : ルビ（ふりがな）配置
///   - RichTextEngine      : リッチテキストエンジン（タグ・禁則・ルビ統合）
///   - VerticalText        : 縦書きテキストレイアウト
///   - FlagManager         : シナリオフラグ管理
///   - ExpressionEvaluator : 条件式・算術式評価器 (@if 用)
///   - ReadTracker         : 既読管理
///   - ChapterManager      : チャプター管理
///   - ScenarioScript      : シナリオスクリプトパーサー
///   - ScenarioMacro       : シナリオマクロ定義・展開
///   - ScenarioState       : シナリオ実行状態のスナップショット
///   - CharacterManager    : キャラクター管理
///   - VNTextureManager    : VN用テクスチャ管理
///   - VNRenderer          : VN描画エンジン
///   - BackgroundManager   : 背景管理
///   - ScreenEffects       : 画面エフェクト
///   - TransitionEngine    : トランジション（場面転換）
///   - ScrollContainer     : スクロールコンテナ
///   - MessageWindow       : メッセージウィンドウ
///   - ChoiceUI            : 選択肢UI
///   - BacklogUI           : バックログUI
///   - ConfirmDialog       : 確認ダイアログ
///   - AutoSkipController  : オートスキップ制御
///   - UISkinLoader        : UIスキンローダー
///   - ConfigScreen        : コンフィグ画面
///   - SaveLoadScreen      : セーブ・ロード画面
///   - AchievementSystem   : 実績システム
///   - CGGallery           : CGギャラリー
///   - FlowChart           : フローチャート

// ── 基盤（依存なし / 最小依存） ────────────────────────────────
#include <mitiru/vn/EasingFunctions.hpp>
#include <mitiru/vn/TrueTypeFont.hpp>
#include <mitiru/vn/NineSlice.hpp>

// ── テキスト処理 ────────────────────────────────────────────
#include <mitiru/vn/TextAnimator.hpp>
#include <mitiru/vn/RubyText.hpp>
#include <mitiru/vn/RichTextEngine.hpp>
#include <mitiru/vn/VerticalText.hpp>

// ── シナリオ・データ管理 ────────────────────────────────────
#include <mitiru/vn/FlagManager.hpp>
#include <mitiru/vn/ExpressionEvaluator.hpp>
#include <mitiru/vn/ReadTracker.hpp>
#include <mitiru/vn/ChapterManager.hpp>
#include <mitiru/vn/ScenarioScript.hpp>
#include <mitiru/vn/ScenarioMacro.hpp>
#include <mitiru/vn/ScenarioState.hpp>

// ── キャラクター・アセット ──────────────────────────────────
#include <mitiru/vn/CharacterManager.hpp>
#include <mitiru/vn/Live2DInterface.hpp>
#include <mitiru/vn/VNTextureManager.hpp>
#include <mitiru/vn/VNRenderer.hpp>
#include <mitiru/vn/BackgroundManager.hpp>
#include <mitiru/vn/MoviePlayer.hpp>

// ── 演出・エフェクト ────────────────────────────────────────
#include <mitiru/vn/ScreenEffects.hpp>
#include <mitiru/vn/TransitionEngine.hpp>
#include <mitiru/vn/DynamicBGM.hpp>

// ── UI部品 ──────────────────────────────────────────────────
#include <mitiru/vn/ScrollContainer.hpp>
#include <mitiru/vn/MessageWindow.hpp>
#include <mitiru/vn/ChoiceUI.hpp>
#include <mitiru/vn/BacklogUI.hpp>
#include <mitiru/vn/ConfirmDialog.hpp>

// ── システム制御 ────────────────────────────────────────────
#include <mitiru/vn/AutoSkipController.hpp>
#include <mitiru/vn/AutoSkipBinding.hpp>
#include <mitiru/vn/UISkinLoader.hpp>
#include <mitiru/vn/ConfigScreen.hpp>
#include <mitiru/vn/SaveLoadScreen.hpp>

// ── メタ・ギャラリー ────────────────────────────────────────
#include <mitiru/vn/AchievementSystem.hpp>
#include <mitiru/vn/CGGallery.hpp>
#include <mitiru/vn/FlowChart.hpp>

// ── 外部連携・拡張 ──────────────────────────────────────────
#include <mitiru/vn/ScriptingHook.hpp>
#include <mitiru/vn/PlayerAnalytics.hpp>
#include <mitiru/vn/Accessibility.hpp>
#include <mitiru/vn/HotReloadScenario.hpp>
