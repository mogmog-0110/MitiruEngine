#pragma once

/// @file AiPipeline.hpp
/// @brief AIパイプライン アンブレラヘッダー
/// @details Mitiru AI自動化パイプラインの全公開ヘッダーを一括インクルードする。
///          AIエージェントがゲームのビルド・実行・観測・操作・検証を行うための
///          統合APIを提供する。
///
/// 含まれるモジュール:
/// - AiSession: エンジンを内包した高レベル自動操作セッション
/// - AiTestRunner: AI駆動テストランナー
/// - AiDebugger: スクリーンショット比較・フレーム解析・二分探索デバッグ
///
/// @code
/// #include <mitiru/ai/AiPipeline.hpp>
///
/// mitiru::ai::AiSession session;
/// session.init({.headless = true});
/// MyGame game;
/// session.loadGame(game);
/// session.step(100);
///
/// mitiru::ai::AiDebugger debugger(session);
/// debugger.markBaseline();
/// session.step(50);
/// auto diff = debugger.diffFromBaseline();
/// @endcode

#include <mitiru/ai/AiSession.hpp>
#include <mitiru/ai/AiTestRunner.hpp>
#include <mitiru/ai/AiDebugger.hpp>
