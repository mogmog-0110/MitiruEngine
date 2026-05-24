#pragma once

/// @file App.hpp
/// @brief フレームワーク非依存のアプリケーション基底クラス
///
/// シーンマネージャ・共有データ・デルタタイム・フェードオーバーレイを統合する。
/// フレームワーク固有のメインループは派生クラス（siv3d::App等）で実装する。
///
/// @code
/// struct GameData { int score = 0; };
///
/// class TitleScene : public sgc::AppScene<GameData> {
/// public:
///     using AppScene::AppScene;
///     void update(float dt) override { /* ... */ }
///     void draw() const override { /* ... */ }
/// };
///
/// // フレームワーク固有の派生クラス
/// class MyApp : public sgc::AppBase<GameData> {
/// public:
///     void run() {
///         getClock().reset();
///         while (frameworkUpdate()) {
///             if (tickFrame()) break;
///         }
///     }
/// };
/// @endcode

#include <concepts>
#include <memory>
#include <utility>

#include "sgc/core/Timer.hpp"
#include "sgc/graphics/IRenderer.hpp"
#include "sgc/scene/SceneManager.hpp"

namespace sgc
{

// 前方宣言
template <typename SharedData>
class AppBase;

/// @brief AppBase用シーン基底クラス（フレームワーク非依存）
/// @tparam SharedData シーン間共有データ型
///
/// sgc::Sceneを継承しつつ、共有データとシーンマネージャへのアクセスを提供する。
/// 各シーンはこのクラスを継承する。
template <typename SharedData>
class AppScene : public Scene
{
public:
	/// @brief コンストラクタ
	/// @param app アプリケーションへのポインタ
	explicit AppScene(AppBase<SharedData>* app) : m_app(app) {}

	/// @brief 仮想デストラクタ
	~AppScene() override = default;

	/// @brief 共有データへの参照を取得する
	/// @return 共有データ
	[[nodiscard]] SharedData& getData() noexcept { return m_app->getData(); }

	/// @brief 共有データへのconst参照を取得する
	/// @return 共有データ（読み取り専用）
	[[nodiscard]] const SharedData& getData() const noexcept { return m_app->getData(); }

	/// @brief シーンマネージャへの参照を取得する（シーン遷移用）
	/// @return シーンマネージャ
	[[nodiscard]] SceneManager& getSceneManager() noexcept { return m_app->getSceneManager(); }

protected:
	AppBase<SharedData>* m_app;  ///< アプリケーションポインタ
};

/// @brief フレームワーク非依存のアプリケーション基底クラス
/// @tparam SharedData シーン間で共有するデータ型
///
/// シーンマネージャ、共有データ、デルタクロック、フェードオーバーレイを統合する。
/// 派生クラスで run() を実装し、フレームワーク固有のメインループを構成する。
template <typename SharedData>
class AppBase
{
public:
	/// @brief コンストラクタ
	/// @param data 共有データの初期値
	explicit AppBase(const SharedData& data = {})
		: m_sharedData(data)
	{
	}

	/// @brief 仮想デストラクタ
	virtual ~AppBase() = default;

	/// @brief シーンを登録してスタックに積む
	/// @tparam S AppScene<SharedData>派生のシーンクラス
	template <typename S>
		requires std::derived_from<S, AppScene<SharedData>>
	void addScene()
	{
		m_sceneManager.template push<S>(this);
	}

	/// @brief シーンをID指定で登録する
	///
	/// ファクトリはAppBase*をコンストラクタに渡すAppScene派生クラスを生成する。
	/// tickFrame()の前にsetInitialScene()で初期シーンを設定する。
	///
	/// @tparam S AppScene<SharedData>派生のシーンクラス
	/// @param id シーンID（Hash.hppの_hashで生成）
	///
	/// @code
	/// app.registerScene<TitleScene>("title"_hash);
	/// app.registerScene<GameScene>("game"_hash);
	/// app.setInitialScene("title"_hash);
	/// @endcode
	template <typename S>
		requires std::derived_from<S, AppScene<SharedData>>
	void registerScene(SceneId id)
	{
		m_sceneManager.registerScene(id, [this]() -> std::unique_ptr<Scene>
		{
			return std::make_unique<S>(this);
		});
	}

	/// @brief ID指定で初期シーンを設定する
	/// @param id シーンID
	/// @return 登録済みIDならtrue
	bool setInitialScene(SceneId id)
	{
		return m_sceneManager.pushScene(id);
	}

	/// @brief 共有データへの参照を取得する
	/// @return 共有データ
	[[nodiscard]] SharedData& getData() noexcept { return m_sharedData; }

	/// @brief 共有データへのconst参照を取得する
	/// @return 共有データ（読み取り専用）
	[[nodiscard]] const SharedData& getData() const noexcept { return m_sharedData; }

	/// @brief シーンマネージャへの参照を取得する
	/// @return シーンマネージャ
	[[nodiscard]] SceneManager& getSceneManager() noexcept { return m_sceneManager; }

	/// @brief シーンマネージャへのconst参照を取得する
	/// @return シーンマネージャ（読み取り専用）
	[[nodiscard]] const SceneManager& getSceneManager() const noexcept { return m_sceneManager; }

	/// @brief デルタクロックへの参照を取得する（タイムスケール調整用）
	/// @return デルタクロック
	[[nodiscard]] DeltaClock& getClock() noexcept { return m_clock; }

	/// @brief レンダラーを設定する（フェードオーバーレイ描画用）
	/// @param renderer レンダラーへのポインタ（所有権は移動しない）
	void setRenderer(IRenderer* renderer) noexcept { m_renderer = renderer; }

protected:
	/// @brief 1フレーム分の処理を実行する（update + draw + fade overlay）
	///
	/// 派生クラスの run() から毎フレーム呼び出す。
	/// シーンスタックが空になったらtrueを返す。
	///
	/// @return シーンスタックが空ならtrue（ループ終了）
	bool tickFrame()
	{
		const float dt = m_clock.tick();

		// シーン更新
		m_sceneManager.update(dt);

		// シーンが空になったら終了
		if (m_sceneManager.empty())
		{
			return true;
		}

		// シーン描画
		m_sceneManager.draw();

		// フェードオーバーレイ描画
		const auto fadeState = m_sceneManager.fadeState();
		if (fadeState != FadeState::None && m_renderer)
		{
			const float progress = m_sceneManager.getFadeProgress();
			const float alpha = (fadeState == FadeState::FadingOut) ? progress : (1.0f - progress);
			m_renderer->drawFadeOverlay(alpha);
		}

		return false;
	}

	SceneManager m_sceneManager;        ///< シーンマネージャ
	SharedData m_sharedData;            ///< シーン間共有データ
	DeltaClock m_clock;                 ///< フレーム間デルタタイム計測
	IRenderer* m_renderer{nullptr};     ///< レンダラー（フェードオーバーレイ用）
};

} // namespace sgc
