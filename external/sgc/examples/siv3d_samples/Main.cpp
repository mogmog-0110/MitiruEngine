/// @file Main.cpp
/// @brief SGCサンプルギャラリー Siv3Dエントリーポイント
///
/// 全サンプルシーンをメニューから選択して実行できるビジュアルギャラリー。

#include <Siv3D.hpp>
#include "sgc/core/Hash.hpp"
#include "sgc/siv3d/Siv3D.hpp"

// シーン定義
#include "SharedData.hpp"
#include "KeyCodes.hpp"
#include "MenuScene.hpp"
#include "SampleTween.hpp"
#include "SampleParticle.hpp"
#include "SamplePhysics.hpp"
#include "SampleRaycast.hpp"
#include "SampleECS.hpp"
#include "SampleUI.hpp"
#include "SampleQuadtree.hpp"
#include "SampleStateMachine.hpp"
#include "SampleObjectPool.hpp"
#include "SampleBehaviorTree.hpp"
#include "SampleMath.hpp"
#include "SampleFade.hpp"
#include "SampleCommand.hpp"
#include "SampleGrid.hpp"
#include "SampleEventSystem.hpp"
#include "SampleCoroutine.hpp"
#include "SampleInputCombo.hpp"
#include "SampleActionMap.hpp"
#include "SampleDebugOverlay.hpp"
#include "SampleUIWidgets.hpp"
#include "SampleTweenTimeline.hpp"
#include "SampleNoise.hpp"
#include "SampleInputMode.hpp"
#include "SampleObserver.hpp"
#include "SampleServiceLocator.hpp"
#include "SampleThreadPool.hpp"
#include "SampleTimer.hpp"
#include "SampleCamera.hpp"
#include "SampleConfig.hpp"
#include "SampleFixedTimestep.hpp"
#include "SampleMemory.hpp"
#include "SampleStateSync.hpp"
#include "SampleLogger.hpp"
#include "SampleMessageChannel.hpp"
#include "SampleOctree.hpp"
#include "SampleHudLayout.hpp"
#include "SamplePendingAction.hpp"
#include "SampleToggleRadio.hpp"
#include "SamplePanelStack.hpp"
#include "SampleTooltipToast.hpp"
#include "SampleScrollList.hpp"
#include "SampleTextLayout.hpp"
#include "SampleSettingsScreen.hpp"
#include "SampleInventoryUI.hpp"
#include "SampleDialogUI.hpp"
#include "SampleDraggableWindow.hpp"
#include "SampleSpeechBubble.hpp"
#include "SampleGlossaryPanel.hpp"

using namespace sgc::literals;

/// @brief SGCサンプルギャラリーのメインエントリーポイント
///
/// sgcのアダプター層を使い、各サンプルシーンを登録する。
/// MenuSceneをハブとして各デモに遷移する。
void Main()
{
	s3d::Window::Resize(800, 600);
	s3d::Window::SetTitle(U"SGC Sample Gallery");
	s3d::Scene::SetBackground(s3d::ColorF{0.1, 0.1, 0.12});

	// ESCキーでアプリを終了しないようにする（×ボタンのみ終了）
	// サンプルシーン内でESC→メニュー戻りに使うため
	s3d::System::SetTerminationTriggers(s3d::UserAction::CloseButtonClicked);

	// レンダラー初期化
	sgc::siv3d::Siv3DRenderer renderer;

	// テキストレンダラー初期化
	sgc::siv3d::Siv3DTextRenderer textRenderer;
	textRenderer.registerFont(48, s3d::Typeface::Heavy);
	textRenderer.registerFont(36, s3d::Typeface::Medium);
	textRenderer.registerFont(32, s3d::Typeface::Medium);
	textRenderer.registerFont(28, s3d::Typeface::Medium);
	textRenderer.registerFont(24, s3d::Typeface::Medium);
	textRenderer.registerFont(22, s3d::Typeface::Regular);
	textRenderer.registerFont(20, s3d::Typeface::Regular);
	textRenderer.registerFont(18, s3d::Typeface::Regular);
	textRenderer.registerFont(16, s3d::Typeface::Regular);
	textRenderer.registerFont(14, s3d::Typeface::Regular);
	textRenderer.registerFont(12, s3d::Typeface::Regular);
	textRenderer.registerFont(11, s3d::Typeface::Regular);
	textRenderer.registerFont(10, s3d::Typeface::Regular);
	textRenderer.registerFont(9, s3d::Typeface::Regular);

	// 入力プロバイダー初期化
	sgc::siv3d::Siv3DInputProvider inputProvider;
	inputProvider.addKey(s3d::KeyEscape, KeyCode::ESCAPE);
	inputProvider.addKey(s3d::KeyEnter, KeyCode::ENTER);
	inputProvider.addKey(s3d::KeySpace, KeyCode::SPACE);
	inputProvider.addKey(s3d::KeyLeft, KeyCode::LEFT);
	inputProvider.addKey(s3d::KeyRight, KeyCode::RIGHT);
	inputProvider.addKey(s3d::KeyUp, KeyCode::UP);
	inputProvider.addKey(s3d::KeyDown, KeyCode::DOWN);
	inputProvider.addKey(s3d::KeyBackspace, KeyCode::BACKSPACE);
	inputProvider.addKey(s3d::KeyW, KeyCode::W);
	inputProvider.addKey(s3d::KeyA, KeyCode::A);
	inputProvider.addKey(s3d::KeyS, KeyCode::S);
	inputProvider.addKey(s3d::KeyD, KeyCode::D);
	inputProvider.addKey(s3d::KeyR, KeyCode::R);
	inputProvider.addKey(s3d::Key1, KeyCode::NUM1);
	inputProvider.addKey(s3d::Key2, KeyCode::NUM2);
	inputProvider.addKey(s3d::Key3, KeyCode::NUM3);

	// アプリケーション起動
	sgc::siv3d::App<SharedData> app;
	app.getData().renderer = &renderer;
	app.getData().textRenderer = &textRenderer;
	app.getData().textMeasure = &textRenderer;
	app.getData().inputProvider = &inputProvider;
	app.getData().screenWidth = 800.0f;
	app.getData().screenHeight = 600.0f;
	app.setRenderer(&renderer);

	// シーン登録
	app.registerScene<MenuScene>("menu"_hash);
	app.registerScene<SampleTween>("tween"_hash);
	app.registerScene<SampleParticle>("particle"_hash);
	app.registerScene<SamplePhysics>("physics"_hash);
	app.registerScene<SampleRaycast>("raycast"_hash);
	app.registerScene<SampleECS>("ecs"_hash);
	app.registerScene<SampleUI>("ui"_hash);
	app.registerScene<SampleQuadtree>("quadtree"_hash);
	app.registerScene<SampleStateMachine>("state"_hash);
	app.registerScene<SampleObjectPool>("pool"_hash);
	app.registerScene<SampleBehaviorTree>("ai"_hash);
	app.registerScene<SampleMath>("math"_hash);
	app.registerScene<SampleFade>("fade"_hash);
	app.registerScene<SampleCommand>("command"_hash);
	app.registerScene<SampleGrid>("grid"_hash);
	app.registerScene<SampleEventSystem>("event"_hash);
	app.registerScene<SampleCoroutine>("coroutine"_hash);
	app.registerScene<SampleInputCombo>("combo"_hash);
	app.registerScene<SampleActionMap>("actionmap"_hash);
	app.registerScene<SampleDebugOverlay>("debug"_hash);
	app.registerScene<SampleUIWidgets>("uiwidgets"_hash);
	app.registerScene<SampleTweenTimeline>("tweentl"_hash);
	app.registerScene<SampleNoise>("noise"_hash);
	app.registerScene<SampleInputMode>("inputmode"_hash);
	app.registerScene<SampleObserver>("observer"_hash);
	app.registerScene<SampleServiceLocator>("svclocator"_hash);
	app.registerScene<SampleThreadPool>("threadpool"_hash);
	app.registerScene<SampleTimer>("timer"_hash);
	app.registerScene<SampleCamera>("camera"_hash);
	app.registerScene<SampleConfig>("config"_hash);
	app.registerScene<SampleFixedTimestep>("fixedts"_hash);
	app.registerScene<SampleMemory>("memory"_hash);
	app.registerScene<SampleStateSync>("statesync"_hash);
	app.registerScene<SampleLogger>("logger"_hash);
	app.registerScene<SampleMessageChannel>("msgchannel"_hash);
	app.registerScene<SampleOctree>("octree"_hash);
	app.registerScene<SampleHudLayout>("hudlayout"_hash);
	app.registerScene<SamplePendingAction>("pending"_hash);
	app.registerScene<SampleToggleRadio>("toggleradio"_hash);
	app.registerScene<SamplePanelStack>("panelstack"_hash);
	app.registerScene<SampleTooltipToast>("tiptoast"_hash);
	app.registerScene<SampleScrollList>("scrolllist"_hash);
	app.registerScene<SampleTextLayout>("textlayout"_hash);
	app.registerScene<SampleSettingsScreen>("settings"_hash);
	app.registerScene<SampleInventoryUI>("inventory"_hash);
	app.registerScene<SampleDialogUI>("dialog"_hash);
	app.registerScene<SampleDraggableWindow>("drag_window"_hash);
	app.registerScene<SampleSpeechBubble>("speech_bubble"_hash);
	app.registerScene<SampleGlossaryPanel>("glossary_panel"_hash);

	// 初期シーン設定
	app.setInitialScene("menu"_hash);
	app.run();
}
