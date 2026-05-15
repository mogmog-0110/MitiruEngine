#pragma once

/// @file SceneTransitionManager.hpp
/// @brief 統一シーン遷移システム
/// @details フェード・スライド・ズーム・ディゾルブ等の組み込みトランジションと
///          カスタムトランジション登録機能を持つシーン遷移マネージャー。
///          MitiruSceneManagerと連携し、ローディング画面の統合も行う。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/scene/MitiruScene.hpp>

namespace mitiru::scene
{

/// @brief 組み込みトランジション種別
enum class TransitionType : uint8_t
{
	None = 0,    ///< 即座に切り替え
	Fade,        ///< フェード（黒画面経由）
	Slide,       ///< スライド
	Zoom,        ///< ズームイン/アウト
	Dissolve,    ///< クロスディゾルブ
	Custom,      ///< カスタムトランジション
};

/// @brief トランジションフェーズ
enum class TransitionPhase : uint8_t
{
	Idle = 0,    ///< トランジション非実行中
	FadeOut,     ///< 旧シーンのフェードアウト
	Load,        ///< 新シーンのロード中
	FadeIn,      ///< 新シーンのフェードイン
};

/// @brief カスタムトランジション定義
/// @details アルファ値を time(0〜1) から算出するイージング関数。
///          戻り値: 0.0=旧シーン完全表示, 1.0=新シーン完全表示
using TransitionEasing = std::function<float(float t)>;

/// @brief トランジション設定
struct TransitionConfig
{
	TransitionType type = TransitionType::Fade;    ///< トランジション種別
	float duration = 0.5f;                          ///< 全体の所要時間（秒）
	std::string customName;                         ///< カスタムトランジション名（Custom時のみ）
};

/// @brief ローディング進捗コールバック
/// @param progress 進捗率 [0.0, 1.0]
using LoadingProgressCallback = std::function<void(float progress)>;

/// @brief 統一シーン遷移マネージャー
/// @details MitiruSceneManagerを拡張し、トランジション付きのシーン切り替えを提供する。
///          FadeOut → Load → FadeIn の3フェーズで遷移を実行し、
///          遷移中のレンダリングアルファ値を提供する。
///
/// @code
/// mitiru::scene::SceneTransitionManager transitionMgr;
///
/// // カスタムトランジションの登録
/// transitionMgr.registerTransition("wipe", [](float t) { return t; });
///
/// // シーン遷移（フェード、0.5秒）
/// auto titleScene = std::make_unique<TitleScene>();
/// transitionMgr.transitionTo(std::move(titleScene), TransitionType::Fade, 0.5f);
///
/// // ゲームループ内
/// transitionMgr.update(dt);
/// float alpha = transitionMgr.getRenderAlpha();
/// @endcode
class SceneTransitionManager
{
public:
	/// @brief デフォルトコンストラクタ
	SceneTransitionManager() = default;

	// ── トランジション付きシーン操作 ───────────

	/// @brief トランジション付きでシーンを切り替える
	/// @param scene 遷移先シーン
	/// @param type トランジション種別
	/// @param duration 所要時間（秒）
	void transitionTo(
		std::unique_ptr<MitiruScene> scene,
		TransitionType type = TransitionType::Fade,
		float duration = 0.5f)
	{
		if (!scene || m_phase != TransitionPhase::Idle)
		{
			return;
		}

		m_pendingScene = std::move(scene);
		m_config.type = type;
		m_config.duration = std::max(duration, 0.001f);
		m_operation = Operation::Replace;

		beginTransition();
	}

	/// @brief トランジション付きでシーン名を指定して切り替える
	/// @param sceneName 遷移先シーン名（ファクトリ登録済み）
	/// @param type トランジション種別
	/// @param duration 所要時間（秒）
	void transitionTo(
		const std::string& sceneName,
		TransitionType type = TransitionType::Fade,
		float duration = 0.5f)
	{
		if (m_phase != TransitionPhase::Idle)
		{
			return;
		}

		const auto it = m_sceneFactories.find(sceneName);
		if (it == m_sceneFactories.end())
		{
			return;
		}

		m_pendingScene = it->second();
		m_config.type = type;
		m_config.duration = std::max(duration, 0.001f);
		m_operation = Operation::Replace;

		beginTransition();
	}

	/// @brief トランジション付きでシーンをプッシュする
	/// @param scene プッシュするシーン
	/// @param type トランジション種別
	/// @param duration 所要時間（秒）
	void pushWithTransition(
		std::unique_ptr<MitiruScene> scene,
		TransitionType type = TransitionType::Fade,
		float duration = 0.5f)
	{
		if (!scene || m_phase != TransitionPhase::Idle)
		{
			return;
		}

		m_pendingScene = std::move(scene);
		m_config.type = type;
		m_config.duration = std::max(duration, 0.001f);
		m_operation = Operation::Push;

		beginTransition();
	}

	/// @brief トランジション付きでシーンをポップする
	/// @param type トランジション種別
	/// @param duration 所要時間（秒）
	void popWithTransition(
		TransitionType type = TransitionType::Fade,
		float duration = 0.5f)
	{
		if (m_sceneStack.size() <= 1 || m_phase != TransitionPhase::Idle)
		{
			return;
		}

		m_config.type = type;
		m_config.duration = std::max(duration, 0.001f);
		m_operation = Operation::Pop;

		beginTransition();
	}

	// ── フレーム更新 ──────────────────────────

	/// @brief トランジションを進行させる
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (m_phase == TransitionPhase::Idle)
		{
			// 通常のシーン更新
			if (!m_sceneStack.empty())
			{
				m_sceneStack.back()->onUpdate(dt);
			}
			return;
		}

		m_elapsed += dt;
		const float halfDuration = m_config.duration * 0.5f;

		switch (m_phase)
		{
		case TransitionPhase::FadeOut:
			if (m_elapsed >= halfDuration)
			{
				m_phase = TransitionPhase::Load;
				m_elapsed = 0.0f;
				executeSceneSwitch();
			}
			break;

		case TransitionPhase::Load:
			// ロード完了（ヘッダオンリーでは即座に完了）
			if (m_loadingCallback)
			{
				m_loadingCallback(1.0f);
			}
			m_phase = TransitionPhase::FadeIn;
			m_elapsed = 0.0f;
			break;

		case TransitionPhase::FadeIn:
			if (m_elapsed >= halfDuration)
			{
				m_phase = TransitionPhase::Idle;
				m_elapsed = 0.0f;
			}
			break;

		case TransitionPhase::Idle:
			break;
		}
	}

	// ── レンダリング情報 ──────────────────────

	/// @brief 現在のレンダリングアルファ値を取得する
	/// @return アルファ値 [0.0, 1.0]
	/// @details 0.0=完全に旧シーン, 1.0=完全に新シーン。
	///          トランジション非実行中は 1.0 を返す。
	[[nodiscard]] float getRenderAlpha() const noexcept
	{
		if (m_phase == TransitionPhase::Idle)
		{
			return 1.0f;
		}

		const float halfDuration = m_config.duration * 0.5f;
		const float t = std::clamp(m_elapsed / halfDuration, 0.0f, 1.0f);

		switch (m_phase)
		{
		case TransitionPhase::FadeOut:
			return applyEasing(1.0f - t); // 1.0 → 0.0

		case TransitionPhase::Load:
			return 0.0f;

		case TransitionPhase::FadeIn:
			return applyEasing(t); // 0.0 → 1.0

		case TransitionPhase::Idle:
			return 1.0f;
		}

		return 1.0f;
	}

	/// @brief トランジション中か判定する
	/// @return トランジション中なら true
	[[nodiscard]] bool isTransitioning() const noexcept
	{
		return m_phase != TransitionPhase::Idle;
	}

	/// @brief 現在のトランジションフェーズを取得する
	/// @return トランジションフェーズ
	[[nodiscard]] TransitionPhase phase() const noexcept
	{
		return m_phase;
	}

	// ── シーン管理（直接操作） ────────────────

	/// @brief シーンをトランジション無しでプッシュする
	/// @param scene プッシュするシーン
	void pushScene(std::unique_ptr<MitiruScene> scene)
	{
		if (scene)
		{
			scene->onEnter();
			m_sceneStack.push_back(std::move(scene));
		}
	}

	/// @brief 現在のシーンを取得する
	/// @return シーンへのポインタ（空の場合は nullptr）
	[[nodiscard]] MitiruScene* currentScene() noexcept
	{
		return m_sceneStack.empty() ? nullptr : m_sceneStack.back().get();
	}

	/// @brief 現在のシーンを取得する（const版）
	[[nodiscard]] const MitiruScene* currentScene() const noexcept
	{
		return m_sceneStack.empty() ? nullptr : m_sceneStack.back().get();
	}

	/// @brief シーンスタックの深さを取得する
	[[nodiscard]] std::size_t depth() const noexcept
	{
		return m_sceneStack.size();
	}

	// ── カスタムトランジション ────────────────

	/// @brief カスタムトランジションを登録する
	/// @param name トランジション名
	/// @param easing イージング関数 (t: 0→1)
	void registerTransition(const std::string& name, TransitionEasing easing)
	{
		m_customTransitions[name] = std::move(easing);
	}

	/// @brief カスタムトランジション付きでシーンを切り替える
	/// @param scene 遷移先シーン
	/// @param customName カスタムトランジション名
	/// @param duration 所要時間（秒）
	void transitionToCustom(
		std::unique_ptr<MitiruScene> scene,
		const std::string& customName,
		float duration = 0.5f)
	{
		if (!scene || m_phase != TransitionPhase::Idle)
		{
			return;
		}

		const auto it = m_customTransitions.find(customName);
		if (it == m_customTransitions.end())
		{
			return;
		}

		m_pendingScene = std::move(scene);
		m_config.type = TransitionType::Custom;
		m_config.customName = customName;
		m_config.duration = std::max(duration, 0.001f);
		m_operation = Operation::Replace;
		m_activeEasing = it->second;

		beginTransition();
	}

	// ── シーンファクトリ登録 ──────────────────

	/// @brief シーンファクトリを登録する（名前ベース遷移用）
	/// @param name シーン名
	/// @param factory シーン生成関数
	void registerScene(const std::string& name, std::function<std::unique_ptr<MitiruScene>()> factory)
	{
		m_sceneFactories[name] = std::move(factory);
	}

	// ── ローディング画面 ─────────────────────

	/// @brief ローディング進捗コールバックを設定する
	/// @param callback 進捗コールバック
	void setLoadingCallback(LoadingProgressCallback callback)
	{
		m_loadingCallback = std::move(callback);
	}

private:
	/// @brief シーン操作種別
	enum class Operation : uint8_t
	{
		Replace = 0,
		Push,
		Pop,
	};

	/// @brief トランジションを開始する
	void beginTransition()
	{
		if (m_config.type == TransitionType::None)
		{
			// 即座に切り替え
			executeSceneSwitch();
			return;
		}

		m_phase = TransitionPhase::FadeOut;
		m_elapsed = 0.0f;

		if (m_loadingCallback)
		{
			m_loadingCallback(0.0f);
		}
	}

	/// @brief シーンの実切り替えを実行する
	void executeSceneSwitch()
	{
		switch (m_operation)
		{
		case Operation::Replace:
			if (!m_sceneStack.empty())
			{
				m_sceneStack.back()->onExit();
				m_sceneStack.pop_back();
			}
			if (m_pendingScene)
			{
				m_pendingScene->onEnter();
				m_sceneStack.push_back(std::move(m_pendingScene));
			}
			break;

		case Operation::Push:
			if (m_pendingScene)
			{
				m_pendingScene->onEnter();
				m_sceneStack.push_back(std::move(m_pendingScene));
			}
			break;

		case Operation::Pop:
			if (!m_sceneStack.empty())
			{
				m_sceneStack.back()->onExit();
				m_sceneStack.pop_back();
			}
			break;
		}

		m_pendingScene.reset();
	}

	/// @brief イージングを適用する
	/// @param t 正規化された時間 [0, 1]
	/// @return イージング後の値
	[[nodiscard]] float applyEasing(float t) const
	{
		// カスタムイージングが設定されていればそちらを使用
		if (m_config.type == TransitionType::Custom && m_activeEasing)
		{
			return m_activeEasing(t);
		}

		// 組み込みイージング
		switch (m_config.type)
		{
		case TransitionType::Fade:
			// スムースステップ
			return t * t * (3.0f - 2.0f * t);

		case TransitionType::Slide:
			// イーズアウトクアッド
			return t * (2.0f - t);

		case TransitionType::Zoom:
			// イーズインアウトクビック
			return (t < 0.5f)
				? 4.0f * t * t * t
				: 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;

		case TransitionType::Dissolve:
			// リニア
			return t;

		case TransitionType::None:
		case TransitionType::Custom:
			return t;
		}

		return t;
	}

	// ── メンバ変数 ────────────────────────────

	/// @brief シーンスタック（末尾がトップ）
	std::vector<std::unique_ptr<MitiruScene>> m_sceneStack;

	/// @brief 遷移待ちのシーン
	std::unique_ptr<MitiruScene> m_pendingScene;

	/// @brief 現在のトランジションフェーズ
	TransitionPhase m_phase = TransitionPhase::Idle;

	/// @brief 現在のトランジション設定
	TransitionConfig m_config;

	/// @brief 経過時間
	float m_elapsed = 0.0f;

	/// @brief 現在の操作種別
	Operation m_operation = Operation::Replace;

	/// @brief アクティブなカスタムイージング
	TransitionEasing m_activeEasing;

	/// @brief カスタムトランジション登録マップ
	std::unordered_map<std::string, TransitionEasing> m_customTransitions;

	/// @brief シーンファクトリ登録マップ
	std::unordered_map<std::string, std::function<std::unique_ptr<MitiruScene>()>> m_sceneFactories;

	/// @brief ローディング進捗コールバック
	LoadingProgressCallback m_loadingCallback;
};

} // namespace mitiru::scene
