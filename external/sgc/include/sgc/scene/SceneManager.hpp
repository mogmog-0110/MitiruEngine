#pragma once

/// @file SceneManager.hpp
/// @brief スタック方式のシーン管理
///
/// push / pop / replace でシーンを管理する。
/// トップのシーンのみ update() が実行される。
///
/// @code
/// sgc::SceneManager manager;
/// manager.push<TitleScene>();
/// manager.update(dt);
///
/// manager.push<GameScene>(); // TitleScene は onPause
/// manager.update(dt);        // GameScene.update() のみ
///
/// manager.pop();             // TitleScene は onResume
/// @endcode

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sgc/scene/Scene.hpp"

namespace sgc
{

/// @brief シーンID型（Hash.hppの_hashで生成）
using SceneId = std::uint64_t;

/// @brief シーンファクトリ関数型
using SceneFactory = std::function<std::unique_ptr<Scene>()>;

/// @brief フェード状態
enum class FadeState
{
	None,       ///< フェードなし
	FadingOut,  ///< フェードアウト中
	FadingIn    ///< フェードイン中
};

/// @brief スタック方式のシーンマネージャ
///
/// シーンをスタックで管理し、トップシーンのみ更新する。
/// フェード遷移もサポートする。
class SceneManager
{
public:
	/// @brief シーンをスタックに積む
	/// @tparam S Scene派生クラス
	/// @tparam Args コンストラクタ引数型
	/// @param args コンストラクタ引数
	template <typename S, typename... Args>
		requires std::derived_from<S, Scene>
	void push(Args&&... args)
	{
		// 現在のトップをポーズ
		if (!m_scenes.empty())
		{
			m_scenes.back()->onPause();
		}

		auto scene = std::make_unique<S>(std::forward<Args>(args)...);
		scene->onEnter();
		m_scenes.push_back(std::move(scene));
	}

	/// @brief トップシーンをポップする
	void pop()
	{
		if (m_scenes.empty()) return;

		m_scenes.back()->onExit();
		m_scenes.pop_back();

		// 新しいトップをリジューム
		if (!m_scenes.empty())
		{
			m_scenes.back()->onResume();
		}
	}

	/// @brief トップシーンを置換する
	/// @tparam S Scene派生クラス
	/// @tparam Args コンストラクタ引数型
	/// @param args コンストラクタ引数
	template <typename S, typename... Args>
		requires std::derived_from<S, Scene>
	void replace(Args&&... args)
	{
		if (!m_scenes.empty())
		{
			m_scenes.back()->onExit();
			m_scenes.pop_back();
		}

		auto scene = std::make_unique<S>(std::forward<Args>(args)...);
		scene->onEnter();
		m_scenes.push_back(std::move(scene));
	}

	/// @brief フェード付きでシーンを遷移する
	///
	/// 現在のシーンをフェードアウトしてから新しいシーンをフェードインする。
	/// フェード中もupdate()は呼ばれ続ける。
	///
	/// @tparam S Scene派生クラス
	/// @tparam Args コンストラクタ引数型
	/// @param fadeDuration フェードの合計時間（秒）。アウト/インで半分ずつ
	/// @param args コンストラクタ引数
	template <typename S, typename... Args>
		requires std::derived_from<S, Scene>
	void changeScene(float fadeDuration, Args&&... args)
	{
		m_fadeDuration = fadeDuration / 2.0f;
		m_fadeElapsed = 0.0f;
		m_fadeState = FadeState::FadingOut;
		m_pendingScene = [this, ...capturedArgs = std::forward<Args>(args)]() mutable
		{
			replace<S>(std::move(capturedArgs)...);
		};
	}

	/// @brief トップシーンを更新する（フェード状態遷移も処理）
	/// @param dt デルタタイム（秒）
	void update(float dt)
	{
		if (m_fadeState == FadeState::FadingOut)
		{
			m_fadeElapsed += dt;
			if (m_fadeElapsed >= m_fadeDuration)
			{
				// フェードアウト完了 → シーン切り替え → フェードイン開始
				if (m_pendingScene)
				{
					m_pendingScene();
					m_pendingScene = nullptr;
				}
				m_fadeElapsed = 0.0f;
				m_fadeState = FadeState::FadingIn;
			}
		}
		else if (m_fadeState == FadeState::FadingIn)
		{
			m_fadeElapsed += dt;
			if (m_fadeElapsed >= m_fadeDuration)
			{
				m_fadeState = FadeState::None;
				m_fadeElapsed = 0.0f;
			}
		}

		if (!m_scenes.empty())
		{
			m_scenes.back()->update(dt);
		}
	}

	/// @brief 描画を行う（フェード状態に応じてdrawFadeIn/drawFadeOutを呼び分ける）
	void draw() const
	{
		if (m_scenes.empty()) return;

		const auto& scene = *m_scenes.back();
		switch (m_fadeState)
		{
		case FadeState::FadingOut:
			scene.drawFadeOut(getFadeProgress());
			break;
		case FadeState::FadingIn:
			scene.drawFadeIn(getFadeProgress());
			break;
		default:
			scene.draw();
			break;
		}
	}

	/// @brief 現在のフェード進行度を返す
	/// @return [0, 1] フェード進行度。フェードなしの場合は0
	[[nodiscard]] float getFadeProgress() const noexcept
	{
		if (m_fadeState == FadeState::None || m_fadeDuration <= 0.0f) return 0.0f;
		float p = m_fadeElapsed / m_fadeDuration;
		if (p > 1.0f) p = 1.0f;
		return p;
	}

	/// @brief 現在のフェード状態を返す
	[[nodiscard]] FadeState fadeState() const noexcept { return m_fadeState; }

	/// @brief シーンスタックが空かどうか
	/// @return 空ならtrue
	[[nodiscard]] bool empty() const noexcept
	{
		return m_scenes.empty();
	}

	/// @brief シーンスタックの深さ
	/// @return スタック内のシーン数
	[[nodiscard]] std::size_t depth() const noexcept
	{
		return m_scenes.size();
	}

	/// @brief トップシーンへのポインタ
	/// @return トップシーン（空ならnullptr）
	[[nodiscard]] Scene* top() const noexcept
	{
		if (m_scenes.empty()) return nullptr;
		return m_scenes.back().get();
	}

	// ── ID指定シーン管理 ─────────────────────────────────

	/// @brief シーンファクトリを登録する
	/// @param id シーンID（Hash.hppの_hashで生成）
	/// @param factory シーン生成関数
	void registerScene(SceneId id, SceneFactory factory)
	{
		m_factories[id] = std::move(factory);
	}

	/// @brief テンプレートでシーンファクトリを登録する
	/// @tparam S Scene派生クラス
	/// @tparam Args コンストラクタ引数型
	/// @param id シーンID
	/// @param args コンストラクタ引数（ファクトリ内でコピーされる）
	template <typename S, typename... Args>
		requires std::derived_from<S, Scene>
	void registerScene(SceneId id, Args&&... args)
	{
		m_factories[id] = [... capturedArgs = std::forward<Args>(args)]() mutable -> std::unique_ptr<Scene>
		{
			return std::make_unique<S>(std::move(capturedArgs)...);
		};
	}

	/// @brief ID指定でフェード付きシーン遷移する
	/// @param id 遷移先シーンID
	/// @param fadeDuration フェードの合計時間（秒）
	/// @return 登録済みIDならtrue、未登録ならfalse
	bool changeScene(SceneId id, float fadeDuration)
	{
		auto it = m_factories.find(id);
		if (it == m_factories.end()) return false;

		m_fadeDuration = fadeDuration / 2.0f;
		m_fadeElapsed = 0.0f;
		m_fadeState = FadeState::FadingOut;
		m_pendingScene = [this, id]()
		{
			replaceScene(id);
		};
		return true;
	}

	/// @brief ID指定でトップシーンを置換する
	/// @param id 遷移先シーンID
	/// @return 登録済みIDならtrue、未登録ならfalse
	bool replaceScene(SceneId id)
	{
		auto it = m_factories.find(id);
		if (it == m_factories.end()) return false;

		if (!m_scenes.empty())
		{
			m_scenes.back()->onExit();
			m_scenes.pop_back();
		}

		auto scene = it->second();
		scene->onEnter();
		m_scenes.push_back(std::move(scene));
		return true;
	}

	/// @brief ID指定でシーンをスタックに積む
	/// @param id シーンID
	/// @return 登録済みIDならtrue、未登録ならfalse
	bool pushScene(SceneId id)
	{
		auto it = m_factories.find(id);
		if (it == m_factories.end()) return false;

		if (!m_scenes.empty())
		{
			m_scenes.back()->onPause();
		}

		auto scene = it->second();
		scene->onEnter();
		m_scenes.push_back(std::move(scene));
		return true;
	}

private:
	std::vector<std::unique_ptr<Scene>> m_scenes;  ///< シーンスタック
	std::unordered_map<SceneId, SceneFactory> m_factories;  ///< シーンファクトリ登録

	// フェード関連
	FadeState m_fadeState{FadeState::None};
	float m_fadeDuration{0.0f};
	float m_fadeElapsed{0.0f};
	std::function<void()> m_pendingScene;  ///< フェード完了後に実行するシーン切り替え
};

} // namespace sgc
