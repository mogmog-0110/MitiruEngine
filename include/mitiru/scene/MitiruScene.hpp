#pragma once

/// @file MitiruScene.hpp
/// @brief Mitiruシーン基底クラスとシーンマネージャー
/// @details observe/controlフック付きのシーン管理システム。
///          スタックベースのシーン管理を提供する。

#include <memory>
#include <string>
#include <vector>

#include <mitiru/core/Screen.hpp>

namespace mitiru::scene
{

/// @brief Mitiruシーン基底クラス
/// @details ゲームシーンのライフサイクルメソッドを定義する抽象基底。
///          派生クラスで具体的なシーンロジックを実装する。
class MitiruScene
{
public:
	/// @brief 仮想デストラクタ
	virtual ~MitiruScene() = default;

	/// @brief シーンに入った時に呼ばれる
	virtual void onEnter() {}

	/// @brief シーンから出る時に呼ばれる
	virtual void onExit() {}

	/// @brief 毎フレーム更新
	/// @param dt デルタタイム（秒）
	virtual void onUpdate(float dt) = 0;

	/// @brief 描画処理
	/// @param screen 描画サーフェス
	virtual void onDraw(Screen& screen) = 0;

	/// @brief シーン名を取得する
	/// @return シーン名
	[[nodiscard]] virtual std::string name() const = 0;

	/// @brief シーンにラベルを追加する
	/// @param label ラベル文字列
	void addLabel(const std::string& label)
	{
		if (!hasLabel(label))
		{
			m_labels.push_back(label);
		}
	}

	/// @brief 指定ラベルが存在するか判定する
	/// @param label 検索するラベル
	/// @return 存在すれば true
	[[nodiscard]] bool hasLabel(const std::string& label) const
	{
		for (const auto& l : m_labels)
		{
			if (l == label)
			{
				return true;
			}
		}
		return false;
	}

	/// @brief ラベル一覧を取得する
	/// @return ラベルのベクタ
	[[nodiscard]] const std::vector<std::string>& labels() const noexcept
	{
		return m_labels;
	}

private:
	std::vector<std::string> m_labels;  ///< セマンティックラベル
};

/// @brief Mitiruシーンマネージャー
/// @details スタックベースでシーンのpush/pop/replaceを管理する。
///
/// @code
/// mitiru::scene::MitiruSceneManager mgr;
/// mgr.pushScene(std::make_unique<TitleScene>());
/// mgr.currentScene()->onUpdate(dt);
/// @endcode
class MitiruSceneManager
{
public:
	/// @brief シーンをスタックにプッシュする
	/// @param scene プッシュするシーン
	void pushScene(std::unique_ptr<MitiruScene> scene)
	{
		if (scene)
		{
			scene->onEnter();
			m_stack.push_back(std::move(scene));
		}
	}

	/// @brief スタックトップのシーンをポップする
	void popScene()
	{
		if (!m_stack.empty())
		{
			m_stack.back()->onExit();
			m_stack.pop_back();
		}
	}

	/// @brief スタックトップのシーンを置換する
	/// @param scene 新しいシーン
	void replaceScene(std::unique_ptr<MitiruScene> scene)
	{
		if (!m_stack.empty())
		{
			m_stack.back()->onExit();
			m_stack.pop_back();
		}
		if (scene)
		{
			scene->onEnter();
			m_stack.push_back(std::move(scene));
		}
	}

	/// @brief 現在のシーン（スタックトップ）を取得する
	/// @return シーンへのポインタ（空の場合は nullptr）
	[[nodiscard]] MitiruScene* currentScene() noexcept
	{
		return m_stack.empty() ? nullptr : m_stack.back().get();
	}

	/// @brief 現在のシーンを取得する（const版）
	/// @return シーンへの const ポインタ
	[[nodiscard]] const MitiruScene* currentScene() const noexcept
	{
		return m_stack.empty() ? nullptr : m_stack.back().get();
	}

	/// @brief シーンスタック全体を取得する（検査用）
	/// @return シーンポインタのベクタ
	[[nodiscard]] std::vector<MitiruScene*> sceneStack() const
	{
		std::vector<MitiruScene*> result;
		result.reserve(m_stack.size());
		for (const auto& scene : m_stack)
		{
			result.push_back(scene.get());
		}
		return result;
	}

	/// @brief スタック深さを取得する
	/// @return スタックに積まれているシーン数
	[[nodiscard]] std::size_t depth() const noexcept
	{
		return m_stack.size();
	}

	/// @brief スタックが空か判定する
	/// @return 空なら true
	[[nodiscard]] bool empty() const noexcept
	{
		return m_stack.empty();
	}

	/// @brief 現在のシーン情報をJSON文字列として返す
	/// @return JSON形式の文字列
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"depth\":" + std::to_string(m_stack.size()) + ",";
		json += "\"stack\":[";
		for (std::size_t i = 0; i < m_stack.size(); ++i)
		{
			if (i > 0)
			{
				json += ",";
			}
			json += "\"" + m_stack[i]->name() + "\"";
		}
		json += "]";
		json += "}";
		return json;
	}

private:
	/// @brief シーンスタック（末尾がトップ）
	std::vector<std::unique_ptr<MitiruScene>> m_stack;
};

} // namespace mitiru::scene
