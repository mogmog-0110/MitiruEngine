#pragma once

/// @file SceneAdapter.hpp
/// @brief Siv3Dゲームループ × sgc::AppBase 統合
///
/// Siv3Dの `while (System::Update())` ループと
/// sgcのAppBaseを統合するAppクラス。
/// シーン間で共有データを持ち回ることができる。
///
/// @code
/// struct GameData { int score = 0; };
///
/// class TitleScene : public sgc::siv3d::AppScene<GameData> {
/// public:
///     using AppScene::AppScene;
///     void update(float dt) override { /* ... */ }
///     void draw() const override { /* ... */ }
/// };
///
/// void Main() {
///     sgc::siv3d::App<GameData> app;
///     app.registerScene<TitleScene>("title"_hash);
///     app.setInitialScene("title"_hash);
///     app.run();
/// }
/// @endcode

#include <Siv3D.hpp>

#include "sgc/scene/App.hpp"

namespace sgc::siv3d
{

/// @brief Siv3D用AppSceneエイリアス（後方互換）
/// @tparam SharedData シーン間共有データ型
template <typename SharedData>
using AppScene = sgc::AppScene<SharedData>;

/// @brief Siv3Dアプリケーションクラス
/// @tparam SharedData シーン間で共有するデータ型
///
/// AppBaseを継承し、Siv3Dのメインループのみを追加する。
/// シーン管理・共有データ・フェードオーバーレイはAppBaseが提供する。
template <typename SharedData>
class App : public sgc::AppBase<SharedData>
{
public:
	using sgc::AppBase<SharedData>::AppBase;

	/// @brief メインループを実行する
	///
	/// Siv3Dの `while (System::Update())` を内包し、
	/// 毎フレームtickFrame()でシーン更新・描画を行う。
	/// シーンスタックが空になるか、Siv3Dが終了するまでループする。
	void run()
	{
		this->m_clock.reset();

		while (s3d::System::Update())
		{
			if (this->tickFrame())
			{
				break;
			}
		}
	}
};

} // namespace sgc::siv3d
