#pragma once

/// @file Scene.hpp
/// @brief シーン基底クラス
///
/// ゲームシーン（タイトル画面、ゲームプレイ、ポーズ画面等）の
/// ライフサイクルインターフェースを定義する。
///
/// @code
/// class TitleScene : public sgc::Scene {
/// public:
///     void onEnter() override { /* リソース読み込み */ }
///     void update(float dt) override { /* 更新 */ }
///     void onExit() override { /* リソース解放 */ }
/// };
/// @endcode

namespace sgc
{

/// @brief シーン基底クラス
///
/// 派生クラスで各ライフサイクルメソッドをオーバーライドする。
/// update()のみ純粋仮想。
class Scene
{
public:
	/// @brief 仮想デストラクタ
	virtual ~Scene() = default;

	/// @brief シーンに入った時に呼ばれる
	virtual void onEnter() {}

	/// @brief シーンから出る時に呼ばれる
	virtual void onExit() {}

	/// @brief シーンが一時停止した時に呼ばれる（上にシーンが積まれた）
	virtual void onPause() {}

	/// @brief シーンが再開した時に呼ばれる（上のシーンがポップされた）
	virtual void onResume() {}

	/// @brief 毎フレーム更新
	/// @param dt デルタタイム（秒）
	virtual void update(float dt) = 0;

	/// @brief 描画処理
	///
	/// update()とは別に呼び出される。
	/// フレームワーク側で描画タイミングを制御する場合に使用する。
	virtual void draw() const {}

	/// @brief フェードイン中の描画処理
	/// @param t フェード進行度 [0, 1]（0=開始、1=完了）
	///
	/// デフォルトではdraw()を呼び出す。フェード演出をカスタマイズする場合にオーバーライドする。
	virtual void drawFadeIn(float) const { draw(); }

	/// @brief フェードアウト中の描画処理
	/// @param t フェード進行度 [0, 1]（0=開始、1=完了）
	///
	/// デフォルトではdraw()を呼び出す。フェード演出をカスタマイズする場合にオーバーライドする。
	virtual void drawFadeOut(float) const { draw(); }
};

} // namespace sgc
