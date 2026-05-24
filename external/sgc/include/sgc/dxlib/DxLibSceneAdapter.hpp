#pragma once

/// @file DxLibSceneAdapter.hpp
/// @brief DxLibゲームループ × sgc::AppBase 統合
///
/// DxLibの ProcessMessage() / ClearDrawScreen() / ScreenFlip() ループと
/// sgcのAppBaseを統合するAppクラス。
/// シーン間で共有データを持ち回ることができる。
///
/// @code
/// struct GameData { int score = 0; };
///
/// class TitleScene : public sgc::dxlib::AppScene<GameData> {
/// public:
///     using AppScene::AppScene;
///     void update(float dt) override { /* ... */ }
///     void draw() const override { /* ... */ }
/// };
///
/// int WINAPI WinMain(...) {
///     // DxLib初期化 ...
///     sgc::dxlib::App<GameData> app;
///     app.registerScene<TitleScene>("title"_hash);
///     app.setInitialScene("title"_hash);
///     app.run();
///     // DxLib終了 ...
/// }
/// @endcode

#include <DxLib.h>

#include "sgc/scene/App.hpp"

namespace sgc::dxlib
{

/// @brief DxLib用AppSceneエイリアス（後方互換）
/// @tparam SharedData シーン間共有データ型
template <typename SharedData>
using AppScene = sgc::AppScene<SharedData>;

/// @brief DxLibアプリケーションクラス
/// @tparam SharedData シーン間で共有するデータ型
///
/// AppBaseを継承し、DxLibのメインループのみを追加する。
/// シーン管理・共有データ・フェードオーバーレイはAppBaseが提供する。
template <typename SharedData>
class App : public sgc::AppBase<SharedData>
{
public:
	using sgc::AppBase<SharedData>::AppBase;

	/// @brief メインループを実行する
	///
	/// DxLibの ProcessMessage() でメッセージ処理を行い、
	/// ClearDrawScreen() / ScreenFlip() で画面を更新する。
	/// シーンスタックが空になるか、ProcessMessageが-1を返すまでループする。
	void run()
	{
		this->m_clock.reset();

		while (ProcessMessage() == 0)
		{
			ClearDrawScreen();
			if (this->tickFrame())
			{
				break;
			}
			ScreenFlip();
		}
	}
};

} // namespace sgc::dxlib
